import csv
import os
import matplotlib.pyplot as plt

CSV_PATH = "parametros/resultados.csv"
OUT_DIR = "parametros/graficos"
os.makedirs(OUT_DIR, exist_ok=True)

# Lê o CSV sem precisar de pandas
linhas = []
with open(CSV_PATH, newline="", encoding="utf-8") as f:
    for row in csv.DictReader(f):
        linhas.append(row)

# Extrai o tempo da configuração base (prod=1, buf=64, matmul=1, cons=1)
tempo_base = next(
    float(r["tempo_s"]) for r in linhas if r["parametro"] == "baseline"
)

graficos = [
    {
        "parametro": "produtor_threads",
        "valor_base": 1,
        "titulo": "Impacto do Número de Produtores no Tempo de Teste",
        "xlabel": "Número de Threads Produtoras (produtor_threads)",
        "arquivo": "produtor_x_tempo.png",
        "cor": "#1f77b4",
        "escala_categorica": False,
    },
    {
        "parametro": "produtor_buffer",
        "valor_base": 64,
        "titulo": "Impacto do Tamanho do Buffer (SHM) no Tempo de Teste",
        "xlabel": "Tamanho do Buffer Compartilhado (produtor_buffer)",
        "arquivo": "buffer_x_tempo.png",
        "cor": "#ff7f0e",
        "escala_categorica": True,
    },
    {
        "parametro": "matmul_threads",
        "valor_base": 1,
        "titulo": "Impacto das Threads no MatMul no Tempo de Teste",
        "xlabel": "Número de Threads no MatMul (matmul_threads)",
        "arquivo": "matmul_x_tempo.png",
        "cor": "#9467bd",
        "escala_categorica": False,
    },
    {
        "parametro": "consumidores",
        "valor_base": 1,
        "titulo": "Impacto do Número de Consumidores no Tempo de Teste",
        "xlabel": "Número de Threads Consumidoras (consumidores)",
        "arquivo": "consumidor_x_tempo.png",
        "cor": "#2ca02c",
        "escala_categorica": False,
    },
]

for cfg in graficos:
    # Junta as variações com o ponto da baseline e ordena pelo valor do parâmetro
    pontos = [
        (int(float(r["valor"])), float(r["tempo_s"]), False)
        for r in linhas
        if r["parametro"] == cfg["parametro"]
    ]
    pontos.append((cfg["valor_base"], tempo_base, True))
    pontos.sort(key=lambda item: item[0])

    valores = [p[0] for p in pontos]
    tempos = [p[1] for p in pontos]
    is_base = [p[2] for p in pontos]

    fig, ax = plt.subplots(figsize=(7.5, 4.5), dpi=150)

    if cfg["escala_categorica"]:
        x_pos = list(range(len(valores)))
        x_labels = [str(v) for v in valores]
    else:
        x_pos = valores
        x_labels = None

    ax.plot(
        x_pos, tempos, marker="o", linewidth=2.2, markersize=8,
        color=cfg["cor"], label="Variações"
    )

    idx_base = is_base.index(True)
    ax.scatter(
        [x_pos[idx_base]], [tempos[idx_base]],
        color="#d62728", s=110, zorder=5, label=f"Baseline ({cfg['valor_base']})"
    )

    y_min, y_max = min(tempos), max(tempos)
    margem = max((y_max - y_min) * 0.18, 0.25)
    ax.set_ylim(max(0, y_min - margem), y_max + margem * 1.4)

    for x, y in zip(x_pos, tempos):
        ax.annotate(
            f"{y:.2f}s",
            (x, y),
            textcoords="offset points",
            xytext=(0, 10),
            ha="center",
            fontsize=9,
            fontweight="bold",
        )

    ax.set_xticks(x_pos)
    if x_labels is not None:
        ax.set_xticklabels(x_labels)

    ax.set_title(cfg["titulo"], fontsize=12, fontweight="bold", pad=12)
    ax.set_xlabel(cfg["xlabel"], fontsize=10)
    ax.set_ylabel("Tempo Total de Teste (s)", fontsize=10)
    ax.grid(True, linestyle="--", alpha=0.4)
    ax.legend(frameon=True)

    plt.tight_layout()
    caminho_saida = os.path.join(OUT_DIR, cfg["arquivo"])
    plt.savefig(caminho_saida)
    plt.close(fig)
    print(f"Gráfico salvo: {caminho_saida}")