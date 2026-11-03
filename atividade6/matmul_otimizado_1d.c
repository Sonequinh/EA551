#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <omp.h>
#include "libmnist.h"

// Aloca matriz em um único bloco contíguo 1D
double *alocar_matriz_1d(uint32_t linhas, uint32_t colunas) {
    double *matriz = (double *)malloc((size_t)linhas * colunas * sizeof(double));
    if (!matriz) {
        perror("Erro ao alocar matriz contígua 1D");
        return NULL;
    }
    return matriz;
}

void liberar_matriz_1d(double *matriz) {
    if (matriz) free(matriz);
}

// Lê o arquivo .emat diretamente para um buffer 1D
double *ler_emat_1d(const char *caminho, uint32_t *linhas, uint32_t *colunas) {
    int fd = open(caminho, O_RDONLY);
    if (fd < 0) {
        perror("Erro ao abrir arquivo .emat");
        return NULL;
    }

    HeaderEmat header;
    if (read(fd, &header, sizeof(HeaderEmat)) != sizeof(HeaderEmat)) {
        fprintf(stderr, "Erro ao ler cabeçalho de %s\n", caminho);
        close(fd);
        return NULL;
    }

    *linhas = header.linhas;
    *colunas = header.colunas;
    size_t total_elementos = (size_t)header.linhas * header.colunas;

    double *matriz = alocar_matriz_1d(header.linhas, header.colunas);
    if (!matriz) {
        close(fd);
        return NULL;
    }

    // Leitura por tipo de dado (uint8, float, double)
    if (header.dtype == 0) { // uint8_t
        uint8_t *buf = malloc(total_elementos * sizeof(uint8_t));
        if (read(fd, buf, total_elementos) == (ssize_t)total_elementos) {
            for (size_t i = 0; i < total_elementos; i++) matriz[i] = (double)buf[i];
        }
        free(buf);
    } else if (header.dtype == 1) { // float
        float *buf = malloc(total_elementos * sizeof(float));
        if (read(fd, buf, total_elementos * sizeof(float)) == (ssize_t)(total_elementos * sizeof(float))) {
            for (size_t i = 0; i < total_elementos; i++) matriz[i] = (double)buf[i];
        }
        free(buf);
    } else if (header.dtype == 2) { // double
        if (read(fd, matriz, total_elementos * sizeof(double)) != (ssize_t)(total_elementos * sizeof(double))) {
            fprintf(stderr, "Erro ao ler dados double de %s\n", caminho);
        }
    } else {
        fprintf(stderr, "dtype %u desconhecido\n", header.dtype);
        liberar_matriz_1d(matriz);
        close(fd);
        return NULL;
    }

    close(fd);
    return matriz;
}

// Multiplicação de Matrizes Otimizada (Layout 1D + Loop IKJ + OpenMP)
double *multiplicar_matrizes_otimizado(const double *A, uint32_t rA, uint32_t cA,
                                       const double *B, uint32_t rB, uint32_t cB) {
    if (cA != rB) {
        fprintf(stderr, "Erro de dimensões: (%ux%u) x (%ux%u)\n", rA, cA, rB, cB);
        return NULL;
    }

    double *C = alocar_matriz_1d(rA, cB);
    if (!C) return NULL;

    // Inicializa a matriz C com zeros
    memset(C, 0, (size_t)rA * cB * sizeof(double));

    // Reordenação IKJ paralela
    #pragma omp parallel for collapse(1) schedule(static)
    for (uint32_t i = 0; i < rA; i++) {
        for (uint32_t k = 0; k < cA; k++) {
            double temp = A[i * cA + k];
            uint32_t idx_B = k * cB;
            uint32_t idx_C = i * cB;
            
            #pragma omp simd
            for (uint32_t j = 0; j < cB; j++) {
                C[idx_C + j] += temp * B[idx_B + j];
            }
        }
    }

    return C;
}