#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include "matmul.h"

// Aloca matriz utilizando o ponteiro de ponteiros (double **)
double **alocar_matriz(uint32_t linhas, uint32_t colunas) {
    double **matriz = malloc(linhas * sizeof(double *));
    if (!matriz) {
        perror("Erro ao alocar linhas da matriz");
        return NULL;
    }
    for (uint32_t i = 0; i < linhas; i++) {
        matriz[i] = malloc(colunas * sizeof(double));
        if (!matriz[i]) {
            perror("Erro ao alocar colunas da matriz");
            liberar_matriz(matriz, i);
            return NULL;
        }
    }
    return matriz;
}

void liberar_matriz(double **matriz, uint32_t linhas) {
    if (!matriz) return;
    for (uint32_t i = 0; i < linhas; i++) {
        free(matriz[i]);
    }
    free(matriz);
}

// Lê o arquivo .emat usando estritamente a chamada de sistema read()
double **ler_emat(const char *caminho, uint32_t *linhas, uint32_t *colunas) {
    int fd = open(caminho, O_RDONLY);
    if (fd < 0) {
        perror("Erro ao abrir arquivo .emat");
        return NULL;
    }

    HeaderEmat header;
    if (read(fd, &header, sizeof(HeaderEmat)) != sizeof(HeaderEmat)) {
        fprintf(stderr, "Erro: Falha ao ler cabeçalho de %s\n", caminho);
        close(fd);
        return NULL;
    }

    *linhas = header.linhas;
    *colunas = header.colunas;

    double **matriz = alocar_matriz(header.linhas, header.colunas);
    if (!matriz) {
        close(fd);
        return NULL;
    }

    size_t total_elementos = (size_t)header.linhas * header.colunas;

    // 1. CASO 1 BYTE: uint8_t (dtype == 0)
    if (header.dtype == 0) {
        uint8_t *buf = malloc(total_elementos * sizeof(uint8_t));
        if (!buf) {
            perror("Erro de alocação do buffer uint8");
            liberar_matriz(matriz, header.linhas);
            close(fd);
            return NULL;
        }

        ssize_t lidos = read(fd, buf, total_elementos * sizeof(uint8_t));
        if (lidos < 0 || (size_t)lidos != total_elementos * sizeof(uint8_t)) {
            fprintf(stderr, "Erro em %s: Incompleto (esperado %lu bytes, lidos %ld)\n",
                    caminho, total_elementos * sizeof(uint8_t), lidos);
            free(buf);
            liberar_matriz(matriz, header.linhas);
            close(fd);
            return NULL;
        }

        size_t idx = 0;
        for (uint32_t i = 0; i < header.linhas; i++) {
            for (uint32_t j = 0; j < header.colunas; j++) {
                matriz[i][j] = (double)buf[idx++];
            }
        }
        free(buf);

    // 2. CASO 4 BYTES: float (dtype == 1)
    } else if (header.dtype == 1) {
        float *buf = malloc(total_elementos * sizeof(float));
        if (!buf) {
            perror("Erro de alocação do buffer float");
            liberar_matriz(matriz, header.linhas);
            close(fd);
            return NULL;
        }

        ssize_t lidos = read(fd, buf, total_elementos * sizeof(float));
        if (lidos < 0 || (size_t)lidos != total_elementos * sizeof(float)) {
            fprintf(stderr, "Erro em %s: Incompleto (esperado %lu bytes, lidos %ld)\n",
                    caminho, total_elementos * sizeof(float), lidos);
            free(buf);
            liberar_matriz(matriz, header.linhas);
            close(fd);
            return NULL;
        }

        size_t idx = 0;
        for (uint32_t i = 0; i < header.linhas; i++) {
            for (uint32_t j = 0; j < header.colunas; j++) {
                matriz[i][j] = (double)buf[idx++];
            }
        }
        free(buf);

    // 3. CASO 8 BYTES: double (dtype == 2)
    } else if (header.dtype == 2) {
        double *buf = malloc(total_elementos * sizeof(double));
        if (!buf) {
            perror("Erro de alocação do buffer double");
            liberar_matriz(matriz, header.linhas);
            close(fd);
            return NULL;
        }

        ssize_t lidos = read(fd, buf, total_elementos * sizeof(double));
        if (lidos < 0 || (size_t)lidos != total_elementos * sizeof(double)) {
            fprintf(stderr, "Erro em %s: Incompleto (esperado %lu bytes, lidos %ld)\n",
                    caminho, total_elementos * sizeof(double), lidos);
            free(buf);
            liberar_matriz(matriz, header.linhas);
            close(fd);
            return NULL;
        }

        size_t idx = 0;
        for (uint32_t i = 0; i < header.linhas; i++) {
            for (uint32_t j = 0; j < header.colunas; j++) {
                matriz[i][j] = buf[idx++];
            }
        }
        free(buf);

    } else {
        fprintf(stderr, "Erro: dtype %u não reconhecido no arquivo %s\n", header.dtype, caminho);
        liberar_matriz(matriz, header.linhas);
        close(fd);
        return NULL;
    }

    close(fd);
    return matriz;
}

// Multiplicação de matrizes C = A * B com loops explícitos de Álgebra Linear
double **multiplicar_matrizes(double **A, uint32_t rA, uint32_t cA, 
                              double **B, uint32_t rB, uint32_t cB) {
    // Validação de compatibilidade de dimensões
    if (cA != rB) {
        fprintf(stderr, "Erro de dimensões: Impossível multiplicar (%ux%u) por (%ux%u)\n", 
                rA, cA, rB, cB);
        return NULL;
    }

    double **C = alocar_matriz(rA, cB);
    if (!C) return NULL;

    // Loop triplo explícito: C[i][j] = sum(A[i][k] * B[k][j])
    for (uint32_t i = 0; i < rA; i++) {
        for (uint32_t j = 0; j < cB; j++) {
            C[i][j] = 0.0;
            for (uint32_t k = 0; k < cA; k++) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }

    return C;
}

// Compara duas matrizes com tolerância para ponto flutuante
int comparar_matrizes(double **C1, double **C2, uint32_t linhas, uint32_t colunas) {
    const double TOLERANCIA = 1e-4;
    for (uint32_t i = 0; i < linhas; i++) {
        for (uint32_t j = 0; j < colunas; j++) {
            if (fabs(C1[i][j] - C2[i][j]) > TOLERANCIA) {
                return 0; // Divergência encontrada
            }
        }
    }
    return 1; // Idênticas
}

// Bateria de testes automatizada
void testar_multiplicacao(int id) {
    char path_a[256], path_b[256], path_c[256];
    snprintf(path_a, sizeof(path_a), "lab4_dados/testes_4.3/A_%d.emat", id);
    snprintf(path_b, sizeof(path_b), "lab4_dados/testes_4.3/B_%d.emat", id);
    snprintf(path_c, sizeof(path_c), "lab4_dados/testes_4.3/C_%d.emat", id);

    printf("========================================\n");
    printf("Executando Teste %d...\n", id);

    uint32_t rA, cA, rB, cB, rC, cC;
    double **A = ler_emat(path_a, &rA, &cA);
    double **B = ler_emat(path_b, &rB, &cB);
    double **C_esperado = ler_emat(path_c, &rC, &cC);

    if (!A || !B) {
        printf("RESULTADO TESTE %d: FALHA na leitura dos arquivos de entrada.\n", id);
        liberar_matriz(A, rA);
        liberar_matriz(B, rB);
        liberar_matriz(C_esperado, rC);
        return;
    }

    printf("Dimensões: A(%ux%u), B(%ux%u)\n", rA, cA, rB, cB);

    double **C_calculado = multiplicar_matrizes(A, rA, cA, B, rB, cB);

    if (!C_calculado) {
        printf("RESULTADO TESTE %d: ERRO ESPERADO/DETECTADO (Dimensões incompatíveis).\n", id);
    } else if (!C_esperado) {
        printf("RESULTADO TESTE %d: Multiplicação concluída (Sem arquivo C de gabarito para comparar).\n", id);
    } else {
        if (rA == rC && cB == cC && comparar_matrizes(C_calculado, C_esperado, rC, cC)) {
            printf("RESULTADO TESTE %d: SUCESSO (Matriz C bateu com o gabarito!).\n", id);
        } else {
            printf("RESULTADO TESTE %d: DIVERGÊNCIA (O resultado calculado difere do gabarito C).\n", id);
        }
    }

    liberar_matriz(A, rA);
    liberar_matriz(B, rB);
    liberar_matriz(C_esperado, rC);
    liberar_matriz(C_calculado, rA);
}

int main(void) {
    // Executa a bateria de testes para todos os pares fornecidos (ex: 1 até 5)
    for (int i = 1; i <= 5; i++) {
        testar_multiplicacao(i);
    }
    return 0;
}