import os
import time
import argparse
import ctypes
from typing import Dict, Any

import tqdm
import torch
import numpy as np
from multiprocessing.shared_memory import SharedMemory
from multiprocessing.resource_tracker import unregister

# Carrega primitivas POSIX para semáforos
libc = ctypes.CDLL(None)

sem_open = libc.sem_open
sem_open.argtypes = [ctypes.c_char_p, ctypes.c_int]
sem_open.restype = ctypes.c_void_p

sem_wait = libc.sem_wait
sem_wait.argtypes = [ctypes.c_void_p]
sem_wait.restype = ctypes.c_int

sem_post = libc.sem_post
sem_post.argtypes = [ctypes.c_void_p]
sem_post.restype = ctypes.c_int

sem_close = libc.sem_close
sem_close.argtypes = [ctypes.c_void_p]
sem_close.restype = ctypes.c_int


class SHMConsumerIterator:
    """Consome amostras da Memória Compartilhada sincronizada via semáforos POSIX"""
    def __init__(self):
        self.shm = SharedMemory(name="shm_mnist")
        self.sem_empty = sem_open(b"/sem_mnist_empty", 0)
        self.sem_full = sem_open(b"/sem_mnist_full", 0)
        self.sem_mutex = sem_open(b"/sem_mnist_mutex", 0)

        # Cabeçalho da SHM: [total_samples (4B), head (4B), tail (4B)]
        self.total_samples = int(np.frombuffer(self.shm.buf[:4], dtype=np.uint32)[0])
        self.num_slots = 2048
        self.header_size = 12
        self.slot_size = 784 + 4  # 784 bytes imagem + 4 bytes rótulo
        self.current = 0
        self.tail = 0

    def __len__(self):
        return self.total_samples

    def __iter__(self):
        return self

    def __next__(self):
        if self.current >= self.total_samples:
            
            sem_close(self.sem_empty)
            sem_close(self.sem_full)
            sem_close(self.sem_mutex)

            unregister(self.shm._name, "shared_memory")
            self.shm.close()
            raise StopIteration

        t_start = time.perf_counter()

        # Aguarda autorização do Produtor em C
        sem_wait(self.sem_full)
        sem_wait(self.sem_mutex)

        slot_idx = self.tail
        self.tail = (self.tail + 1) % self.num_slots

        offset = self.header_size + slot_idx * self.slot_size
        img_bytes = self.shm.buf[offset : offset + 784]
        label_bytes = self.shm.buf[offset + 784 : offset + 788]

        # Converte diretamente da memória RAM sem cópias
        img_np = np.frombuffer(img_bytes, dtype=np.uint8).astype(np.float32)
        image_class = int(np.frombuffer(label_bytes, dtype=np.int32)[0])

        sem_post(self.sem_mutex)
        sem_post(self.sem_empty) # Libera o slot no C para nova produção

        self.current += 1
        image_tensor = torch.from_numpy(img_np).reshape(1, 1, 28, 28)
        
        t_read_sample = time.perf_counter() - t_start
        return image_tensor, image_class, t_read_sample


def train(args: Dict[str, Any]):
    print("Conectando à Memória Compartilhada e aguardando amostras do Produtor C...")
    consumer_stream = SHMConsumerIterator()

    print("Inicialização do modelo de rede neural...")
    model = torch.nn.Linear(28 * 28, 10)

    optimizer = torch.optim.SGD(model.parameters(), lr=args["learning_rate"])
    loss_function = torch.nn.CrossEntropyLoss()

    iterator = tqdm.tqdm(consumer_stream, desc="Consumidor PyTorch via SHM")
    predictions = []
    targets = []
    t_read = 0.0
    t_loop = 0.0

    for image_tensor, image_class, t_read_sample in iterator:
        t0 = time.perf_counter()

        # Tempo gasto para obter a imagem da SHM
        t_read += t_read_sample

        # Linearização da imagem para multiplicação de matrizes: (1, 1, 28, 28) -> (1, 784)
        image_tensor = image_tensor.view(1, -1)

        # ----------------------------- Machine Learning Loop -----------------------------
        output = model(image_tensor)
        loss: torch.Tensor = loss_function(output, torch.tensor([image_class]))

        loss.backward()
        optimizer.step()
        optimizer.zero_grad()

        predictions.append(output.argmax(dim=1).item())
        targets.append(image_class)

        t_loop += (time.perf_counter() - t0) + t_read_sample
        read_pct = 100.0 * t_read / t_loop if t_loop > 0 else 0.0

        iterator.set_postfix(read_pct=f"{read_pct:.1f}%")
        # ---------------------------------------------------------------------------------

    accuracy = (torch.tensor(predictions) == torch.tensor(targets)).float().mean().item()
    print(f"\nAcurácia de classificação de dígitos: {accuracy:.4f}")
    print(f"O consumo de imagens via SHM gastou {read_pct:.1f}% do tempo total do loop.")

    out_dir = args.get("weights_dir", ".")
    os.makedirs(out_dir, exist_ok=True)
    w_path = os.path.join(out_dir, "W.npy")
    np.save(w_path, model.weight.detach().cpu().numpy().astype(np.float32))
    print(f"Pesos salvos em {w_path} (shape {tuple(model.weight.shape)})")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--learning_rate", type=float, default=1e-4)
    parser.add_argument("--weights_dir", type=str, default=".")
    args = parser.parse_args()

    train(vars(args))
    print("Treinamento concluído.")