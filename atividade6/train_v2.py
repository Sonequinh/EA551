import os
import time
import struct
import argparse
import ctypes
import mmap
import posix_ipc
import tqdm
import torch
import torch.nn as nn
import torch.optim as optim
import numpy as np

# ==============================================================================
# 1. Carregamento da Biblioteca Compartilhada C (.so)
# ==============================================================================
LIB_PATH = os.path.abspath("./libmnist.so")
if not os.path.exists(LIB_PATH):
    raise FileNotFoundError(f"Biblioteca '{LIB_PATH}' não encontrada. Compile primeiro com o gcc!")

lib = ctypes.CDLL(LIB_PATH)

# Declaração de assinaturas do C via ctypes
lib.iniciar_produtor.argtypes = [ctypes.c_char_p, ctypes.c_int]
lib.iniciar_produtor.restype = ctypes.c_int

lib.finalizar_produtor.argtypes = []
lib.finalizar_produtor.restype = None

lib.executar_modo_teste_c.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p]
lib.executar_modo_teste_c.restype = ctypes.c_int


# ==============================================================================
# 2. Utilitário para Exportar Pesos no Formato .emat (C-Compatible)
# ==============================================================================
def salvar_pesos_emat(caminho_out: str, weights_tensor: torch.Tensor):
    """
    Salva a matriz de pesos W (10x784) no formato binário .emat (dtype=1 -> float32).
    """
    W = weights_tensor.detach().cpu().numpy().astype(np.float32)
    linhas, colunas = W.shape

    # Cabeçalho .emat: 4B (linhas), 4B (colunas), 1B (dtype=1 para float32)
    header = struct.pack("<IIB", linhas, colunas, 1)

    with open(caminho_out, "wb") as f:
        f.write(header)
        f.write(W.tobytes())

    print(f"\n[Python] Pesos W salvos com sucesso em '{caminho_out}' (Shape: {linhas}x{colunas})")


# ==============================================================================
# 3. Modelo Linear PyTorch
# ==============================================================================
class LogisticRegressionMNIST(nn.Module):
    def __init__(self):
        super(LogisticRegressionMNIST, self).__init__()
        # Matriz W de dimensão (10 x 784) sem viés (bias=False) para ser multiplicada por W * x
        self.linear = nn.Linear(28 * 28, 10, bias=False)

    def forward(self, x):
        return self.linear(x)


# ==============================================================================
# 4. Loop de Treino (C-DataLoader) e Avaliação (C-Modo Teste)
# ==============================================================================
def train_and_evaluate(args: dict):
    dir_treino = os.path.join(args["mnist_path"], "train")
    dir_teste = os.path.join(args["mnist_path"], "test")
    weights_dir = args["weights_dir"]
    os.makedirs(weights_dir, exist_ok=True)

    w_npy_path = os.path.join(weights_dir, "W.npy")
    w_emat_path = os.path.join(weights_dir, "W.emat")

    epochs = args["epochs"]
    batch_size = args["batch_size"]
    num_threads = args["num_threads"]

    # Modelo, Loss e Otimizador
    model = LogisticRegressionMNIST()
    criterion = nn.CrossEntropyLoss()
    optimizer = optim.SGD(model.parameters(), lr=args["learning_rate"])

    print("\n==================== FASE 1: TREINAMENTO (C-DataLoader) ====================")

    for epoch in range(epochs):
        # 1. Inicia o Produtor em C (Atividade 5)
        total_amostras = lib.iniciar_produtor(dir_treino.encode("utf-8"), num_threads)
        if total_amostras <= 0:
            raise RuntimeError(f"Falha ao iniciar o produtor em C para a pasta '{dir_treino}'.")

        # 2. Conecta à SHM (/shm_mnist)
        shm_fd = os.open("/dev/shm/shm_mnist", os.O_RDWR)
        # Tamanho: Header (12B: total_samples, head, tail) + 2048 * Slot (784B img + 4B label)
        buf_size = 12 + 2048 * 788
        shm_mem = mmap.mmap(shm_fd, buf_size, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
        os.close(shm_fd)

        # 3. Conecta aos Semáforos POSIX
        sem_empty = posix_ipc.Semaphore("/sem_mnist_empty")
        sem_full = posix_ipc.Semaphore("/sem_mnist_full")
        sem_mutex = posix_ipc.Semaphore("/sem_mnist_mutex")

        predictions, targets = [], []
        t0 = time.perf_counter()

        pbar = tqdm.tqdm(total=total_amostras, desc=f"Época {epoch+1}/{epochs}")
        amostras_lidas = 0

        while amostras_lidas < total_amostras:
            cur_batch_size = min(batch_size, total_amostras - amostras_lidas)
            batch_imgs, batch_labels = [], []

            # Retira um mini-batch da SHM
            for _ in range(cur_batch_size):
                sem_full.acquire()
                sem_mutex.acquire()

                # Leitura do índice 'tail' na SHM (offset 8)
                tail = struct.unpack("<I", shm_mem[8:12])[0]
                slot_offset = 12 + tail * 788

                # Extrai os pixels uint8 da imagem e o label int32
                img_bytes = shm_mem[slot_offset : slot_offset + 784]
                label = struct.unpack("<i", shm_mem[slot_offset + 784 : slot_offset + 788])[0]

                # Atualiza o índice 'tail' circular
                new_tail = (tail + 1) % 2048
                shm_mem[8:12] = struct.pack("<I", new_tail)

                sem_mutex.release()
                sem_empty.release()

                # Normalização simples (0 a 1) para float32
                img_np = np.frombuffer(img_bytes, dtype=np.uint8).astype(np.float32) / 255.0
                batch_imgs.append(img_np)
                batch_labels.append(label)
                amostras_lidas += 1

            x_batch = torch.tensor(np.array(batch_imgs), dtype=torch.float32)
            y_batch = torch.tensor(np.array(batch_labels), dtype=torch.long)

            # Passos de Treino no PyTorch
            optimizer.zero_grad()
            outputs = model(x_batch)
            loss = criterion(outputs, y_batch)
            loss.backward()
            optimizer.step()

            predictions.extend(outputs.argmax(dim=1).tolist())
            targets.extend(batch_labels)
            pbar.update(cur_batch_size)

        pbar.close()

        # Encerra o Produtor C da época atual
        shm_mem.close()
        sem_empty.close()
        sem_full.close()
        sem_mutex.close()
        lib.finalizar_produtor()

        acc = (torch.tensor(predictions) == torch.tensor(targets)).float().mean().item()
        tempo_ep = time.perf_counter() - t0
        print(f"Época [{epoch+1}/{epochs}] Concluída em {tempo_ep:.2f}s | Acurácia Treino: {acc * 100:.2f}%")

    # 4. Salva os Pesos em ambos os formatos (.npy e .emat)
    weights_matrix = model.linear.weight.detach().cpu().numpy().astype(np.float32)
    np.save(w_npy_path, weights_matrix)
    print(f"\n[Python] Pesos salvos em '{w_npy_path}' (Shape: {weights_matrix.shape})")

    salvar_pesos_emat(w_emat_path, model.linear.weight)

    # ==============================================================================
    # 5. FASE 2: MODO TESTE EM C (Item 4 dos Requisitos)
    # ==============================================================================
    print("\n==================== FASE 2: AVALIAÇÃO GLOBAL (C-Modo Teste) ====================")
    ret = lib.executar_modo_teste_c(
        dir_teste.encode("utf-8"),
        num_threads,
        w_emat_path.encode("utf-8")
    )

    if ret == 0:
        print("[Python] Pipeline completo de treino e teste executado com sucesso!")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Treino PyTorch com C-DataLoader e C-Modo Teste")
    parser.add_argument("--mnist_path", type=str, default="mnist_emat", help="Caminho para o dataset (com pastas 'train' e 'test')")
    parser.add_argument("--learning_rate", type=float, default=1e-3, help="Taxa de aprendizado")
    parser.add_argument("--epochs", type=int, default=5, help="Quantidade de épocas")
    parser.add_argument("--batch_size", type=int, default=64, help="Tamanho do mini-batch no treino")
    parser.add_argument("--num_threads", type=int, default=4, help="Número de threads do produtor C")
    parser.add_argument("--weights_dir", type=str, default=".", help="Diretório onde salvar os pesos")
    args = parser.parse_args()

    train_and_evaluate(vars(args))