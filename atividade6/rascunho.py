import ctypes
import os

# Carrega a biblioteca C
lib = ctypes.CDLL("./libmnist.so")

# Protótipo da função em ctypes
lib.executar_modo_teste_c.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p]
lib.executar_modo_teste_c.restype = ctypes.c_int

# Caminhos do dataset de teste e dos pesos
dir_teste = b"./mnist_emat/test"  # Ajuste para a pasta 'test'
arquivo_pesos = b"./W.emat"
num_threads_produtor = 4

# Validação defensiva
if not os.path.exists(arquivo_pesos.decode()):
    print(f"❌ Erro: Arquivo de pesos '{arquivo_pesos.decode()}' não encontrado!")
    exit(1)

if not os.path.exists(dir_teste.decode()):
    print(f"❌ Erro: Diretório de teste '{dir_teste.decode()}' não encontrado!")
    exit(1)

print("Iniciando Modo Teste (Batch Inference)...")
lib.executar_modo_teste_c(dir_teste, num_threads_produtor, arquivo_pesos)