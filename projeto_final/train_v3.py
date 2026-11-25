import os
import struct
import ctypes
import argparse
from typing import Dict, Any

import tqdm
import torch
import numpy as np

IMG_DIM = 128
IMG_BYTES = IMG_DIM * IMG_DIM  # 16384

SHM_NAME = b"/shm_mnist"
SEM_EMPTY_NAME = b"/sem_mnist_empty"
SEM_FULL_NAME = b"/sem_mnist_full"
SEM_MUTEX_NAME = b"/sem_mnist_mutex"

O_RDWR = 0o2
PROT_READ = 0x1
PROT_WRITE = 0x2
MAP_SHARED = 0x01


class SampleSlot(ctypes.Structure):
    _fields_ = [
        ("image", ctypes.c_uint8 * IMG_BYTES),
        ("label", ctypes.c_int32),
    ]


class SharedBufferHeader(ctypes.Structure):
    _fields_ = [
        ("total_samples", ctypes.c_uint32),
        ("capacity", ctypes.c_uint32),
        ("head", ctypes.c_uint32),
        ("tail", ctypes.c_uint32),
    ]


def shm_buffer_bytes(capacity: int) -> int:
    return ctypes.sizeof(SharedBufferHeader) + capacity * ctypes.sizeof(SampleSlot)


def carregar_biblioteca(lib_path: str):
    lib = ctypes.CDLL(lib_path)
    libc = ctypes.CDLL("libc.so.6")
    librt = ctypes.CDLL("librt.so.1")
    libpthread = ctypes.CDLL("libpthread.so.0")

    lib.iniciar_produtor_ext.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
    lib.iniciar_produtor_ext.restype = ctypes.c_int

    lib.finalizar_produtor.argtypes = []
    lib.finalizar_produtor.restype = None

    lib.executar_modo_teste_ext.argtypes = [
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_char_p,
    ]
    lib.executar_modo_teste_ext.restype = ctypes.c_int

    librt.shm_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_uint]
    librt.shm_open.restype = ctypes.c_int

    libc.mmap.argtypes = [
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_long,
    ]
    libc.mmap.restype = ctypes.c_void_p

    libc.munmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    libc.munmap.restype = ctypes.c_int

    libc.close.argtypes = [ctypes.c_int]
    libc.close.restype = ctypes.c_int

    libpthread.sem_open.argtypes = [ctypes.c_char_p, ctypes.c_int]
    libpthread.sem_open.restype = ctypes.c_void_p

    libpthread.sem_wait.argtypes = [ctypes.c_void_p]
    libpthread.sem_wait.restype = ctypes.c_int

    libpthread.sem_post.argtypes = [ctypes.c_void_p]
    libpthread.sem_post.restype = ctypes.c_int

    libpthread.sem_close.argtypes = [ctypes.c_void_p]
    libpthread.sem_close.restype = ctypes.c_int

    return lib, libc, librt, libpthread


def salvar_w_emat(w_np: np.ndarray, w_emat_path: str) -> None:
    """Salva matriz 2D float32 no formato .emat (header 9 bytes + dados; dtype=1 -> float)."""
    arr = np.ascontiguousarray(w_np, dtype=np.float32)
    linhas, colunas = arr.shape
    with open(w_emat_path, "wb") as f:
        f.write(struct.pack("<IIB", linhas, colunas, 1))
        f.write(arr.tobytes())


def train(args: Dict[str, Any]) -> None:
    lib, libc, librt, libpthread = carregar_biblioteca(args["lib_path"])

    train_dir = os.path.join(args["data_dir"], "train").encode("utf-8")
    test_dir = os.path.join(args["data_dir"], "test").encode("utf-8")

    total_amostras = lib.iniciar_produtor_ext(
        train_dir, args["produtor_threads"], args["buffer_size"]
    )
    if total_amostras <= 0:
        raise RuntimeError(f"Falha ao iniciar produtor em {train_dir.decode()}")

    sem_empty = libpthread.sem_open(SEM_EMPTY_NAME, 0)
    sem_full = libpthread.sem_open(SEM_FULL_NAME, 0)
    sem_mutex = libpthread.sem_open(SEM_MUTEX_NAME, 0)

    shm_bytes = shm_buffer_bytes(args["buffer_size"])
    shm_fd = librt.shm_open(SHM_NAME, O_RDWR, 0o666)
    shm_ptr = libc.mmap(None, shm_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)

    if shm_fd < 0 or shm_ptr in (None, ctypes.c_void_p(-1).value):
        lib.finalizar_produtor()
        raise RuntimeError("Falha ao mapear SHM do produtor em Python")

    header_ptr = ctypes.cast(shm_ptr, ctypes.POINTER(SharedBufferHeader))
    slots_base = ctypes.cast(
        ctypes.c_void_p(shm_ptr + ctypes.sizeof(SharedBufferHeader)),
        ctypes.POINTER(SampleSlot),
    )

    device = torch.device("cuda" if torch.cuda.is_available() and not args["cpu"] else "cpu")
    model = torch.nn.Linear(IMG_BYTES, 10, bias=False).to(device)
    optimizer = torch.optim.SGD(model.parameters(), lr=args["learning_rate"])
    loss_function = torch.nn.CrossEntropyLoss()

    predictions = []
    targets = []

    try:
        iterator = tqdm.tqdm(range(total_amostras), desc="Treinando com libmnist (128x128)")
        for _ in iterator:
            while libpthread.sem_wait(sem_full) != 0:
                pass
            while libpthread.sem_wait(sem_mutex) != 0:
                pass

            tail = header_ptr.contents.tail
            cap = header_ptr.contents.capacity
            slot = slots_base[tail]
            header_ptr.contents.tail = (tail + 1) % cap

            img_copy = np.frombuffer(slot.image, dtype=np.uint8).astype(np.float32).copy()
            label = int(slot.label)

            libpthread.sem_post(sem_mutex)
            libpthread.sem_post(sem_empty)

            if label < 0:
                continue

            x = torch.from_numpy(img_copy).view(1, IMG_BYTES).to(device)
            y_target = torch.tensor([label], dtype=torch.long, device=device)

            output = model(x)
            loss = loss_function(output, y_target)

            loss.backward()
            optimizer.step()
            optimizer.zero_grad()

            predictions.append(int(output.argmax(dim=1).item()))
            targets.append(label)
    finally:
        libc.munmap(shm_ptr, shm_bytes)
        if shm_fd >= 0:
            libc.close(shm_fd)
        if sem_empty:
            libpthread.sem_close(sem_empty)
        if sem_full:
            libpthread.sem_close(sem_full)
        if sem_mutex:
            libpthread.sem_close(sem_mutex)
        lib.finalizar_produtor()

    acc = (torch.tensor(predictions) == torch.tensor(targets)).float().mean().item() if targets else 0.0
    print(f"Acurácia no treino (1 época): {acc:.4f}")

    out_dir = args["weights_dir"]
    os.makedirs(out_dir, exist_ok=True)
    w_np = model.weight.detach().cpu().numpy().astype(np.float32)  # shape (10, 16384)
    w_npy_path = os.path.join(out_dir, "W.npy")
    w_emat_path = os.path.join(out_dir, "W.emat")

    np.save(w_npy_path, w_np)
    salvar_w_emat(w_np, w_emat_path)
    print(f"Pesos salvos em {w_npy_path} e {w_emat_path} (shape {w_np.shape})")

    if os.path.isdir(test_dir.decode("utf-8")):
        print("Executando teste final via libmnist...")
        rc = lib.executar_modo_teste_ext(
            test_dir,
            args["produtor_threads"],
            args["matmul_threads"],
            args["buffer_size"],
            w_emat_path.encode("utf-8"),
        )
        if rc != 0:
            raise RuntimeError("Falha ao executar rotina de teste da biblioteca")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--data_dir", type=str, default="big_mnist_emat")
    parser.add_argument("--lib_path", type=str, default="./libmnist.so")
    parser.add_argument("--learning_rate", type=float, default=1e-4)
    parser.add_argument("--weights_dir", type=str, default=".")
    parser.add_argument("--produtor_threads", type=int, default=4)
    parser.add_argument("--matmul_threads", type=int, default=4)
    parser.add_argument("--buffer_size", type=int, default=2048)
    parser.add_argument("--cpu", action="store_true")
    args = parser.parse_args()
    train(vars(args))