#!/bin/bash
set -e

OUT_DIR="parametros"
mkdir -p "$OUT_DIR"
CSV="${OUT_DIR}/resultados.csv"
echo "experimento,parametro,valor,tempo_s,vazao_img_s,acuracia_pct,arquivo_yaml" > "$CSV"

# Detecta automaticamente o caminho do executável
if [ -x "./runner_yaml" ]; then
    RUNNER="./runner_yaml"
elif [ -x "./compilacao/runner_yaml" ]; then
    RUNNER="./compilacao/runner_yaml"
else
    echo "Erro: executável 'runner_yaml' não encontrado. Compile o projeto antes."
    exit 1
fi

# Configuração padrão (Baseline)
BASE_PROD=1
BASE_BUF=64
BASE_MATMUL=1
BASE_CONS=1

rodar_experimento() {
    local exp_nome="$1"
    local param="$2"
    local valor="$3"
    local prod="$4"
    local buf="$5"
    local matmul="$6"
    local cons="$7"

    local yaml_file="${OUT_DIR}/exp_${exp_nome}.yaml"

    cat <<YAML > "$yaml_file"
# Experimento: ${exp_nome} (${param}=${valor})
modo: test
caminho_pesos: ./W.emat
caminho_entrada: ./big_mnist_emat/test
produtor_threads: ${prod}
produtor_buffer: ${buf}
matmul_threads: ${matmul}
consumidores: ${cons}
visualizacao: false
YAML

    echo "============================================================"
    echo ">>> RODANDO: ${exp_nome} | ${param}=${valor} (${yaml_file})"
    echo "============================================================"

    local saida
    saida=$(LD_LIBRARY_PATH=./lib "$RUNNER" "$yaml_file" | tee /dev/tty)

    local tempo vazao acuracia
    tempo=$(echo "$saida" | awk -F':' '/Tempo Total/{gsub(/[^0-9.]/,"",$2); print $2}')
    vazao=$(echo "$saida" | awk -F':' '/Throughput/{gsub(/[^0-9.]/,"",$2); print $2}')
    acuracia=$(echo "$saida" | awk -F':' '/Global/{gsub(/[^0-9.]/,"",$2); print $2}')

    echo "${exp_nome},${param},${valor},${tempo},${vazao},${acuracia},${yaml_file}" >> "$CSV"
}

# Aquecimento do Page Cache do Linux (não entra no CSV)
echo ">>> Executando warm-up do cache de disco..."
cat <<YAML > "${OUT_DIR}/_warmup.yaml"
modo: test
caminho_pesos: ./W.emat
caminho_entrada: ./big_mnist_emat/test
produtor_threads: 4
produtor_buffer: 1024
matmul_threads: 1
consumidores: 4
visualizacao: false
YAML
LD_LIBRARY_PATH=./lib "$RUNNER" "${OUT_DIR}/_warmup.yaml" > /dev/null
rm -f "${OUT_DIR}/_warmup.yaml"

# 0. Baseline (Configuração Padrão)
rodar_experimento "00_base" "baseline" "base" "$BASE_PROD" "$BASE_BUF" "$BASE_MATMUL" "$BASE_CONS"

# 1. Variação de produtor_threads (base = 1)
for p in 2 4 8; do
    rodar_experimento "prod_${p}" "produtor_threads" "$p" "$p" "$BASE_BUF" "$BASE_MATMUL" "$BASE_CONS"
done

# 2. Variação de produtor_buffer (base = 64)
for b in 1 8 512 2048; do
    rodar_experimento "buf_${b}" "produtor_buffer" "$b" "$BASE_PROD" "$b" "$BASE_MATMUL" "$BASE_CONS"
done

# 3. Variação de matmul_threads (base = 1)
for m in 2 4 5 10; do
    rodar_experimento "matmul_${m}" "matmul_threads" "$m" "$BASE_PROD" "$BASE_BUF" "$m" "$BASE_CONS"
done

# 4. Variação de consumidores (base = 1)
for c in 2 4 8; do
    rodar_experimento "cons_${c}" "consumidores" "$c" "$BASE_PROD" "$BASE_BUF" "$BASE_MATMUL" "$c"
done

echo ""
echo "Concluído! Todos os YAMLs e resultados foram salvos em ./${OUT_DIR}/"
column -t -s',' "$CSV"
