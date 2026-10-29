#ifndef MATMUL_H
#define MATMUL_H

#include <stdint.h>

// Cabeçalho binário do formato .emat (9 bytes)
typedef struct __attribute__((packed)) {
    uint32_t linhas;   // 4 bytes
    uint32_t colunas;  // 4 bytes
    uint8_t  dtype;    // 1 byte (0 = uint8, 1 = double)
} HeaderEmat;

// Funções de gerenciamento de memória
double **alocar_matriz(uint32_t linhas, uint32_t colunas);
void liberar_matriz(double **matriz, uint32_t linhas);

// Leitura via system calls
double **ler_emat(const char *caminho, uint32_t *linhas, uint32_t *colunas);

// Operações e validação
double **multiplicar_matrizes(double **A, uint32_t rA, uint32_t cA, 
                              double **B, uint32_t rB, uint32_t cB);
int comparar_matrizes(double **C1, double **C2, uint32_t linhas, uint32_t colunas);

#endif