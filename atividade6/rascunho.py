import ctypes
import time

# 1. Carrega a biblioteca compartilhada .so
lib = ctypes.CDLL("./libmnist.so")

# 2. Configura as assinaturas das funções (argtypes e restype)
lib.iniciar_produtor.argtypes = [ctypes.c_char_p, ctypes.c_int]
lib.iniciar_produtor.restype = ctypes.c_int

lib.finalizar_produtor.argtypes = []
lib.finalizar_produtor.restype = None

# 3. Chama a função em C
caminho_treino = b"./mnist_emat/train"
num_threads = 4

print("Iniciando produtor em C via .so...")
total = lib.iniciar_produtor(caminho_treino, num_threads)
print(f"Produtor iniciado com sucesso! Total de amostras: {total}")

# Simula 2 segundos de trabalho
time.sleep(2)

print("Finalizando produtor...")
lib.finalizar_produtor()
print("Teste concluído com sucesso!")