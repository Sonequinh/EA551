import sys
import struct
import numpy as np
import matplotlib.pyplot as plt

if len(sys.argv) < 2:
    print("Uso: python3 visualizador.py <caminho_imagem.emat>")
    sys.exit(1)

caminho_imagem = sys.argv[1]

# Lê o arquivo binário .emat
with open(caminho_imagem, "rb") as f:
    header = f.read(9)
    linhas, colunas, dtype = struct.unpack("<IIB", header)
    
    if dtype == 0:
        dt = np.uint8
    elif dtype == 1:
        dt = np.float32
    else:
        dt = np.float64
        
    img_data = np.frombuffer(f.read(), dtype=dt).reshape((linhas, colunas))

# Exibe a janela gráfica
plt.figure("Modo Inferência - MNIST")
plt.imshow(img_data, cmap="gray")
plt.title(f"Imagem: {caminho_imagem}")
plt.axis("off")
plt.show()