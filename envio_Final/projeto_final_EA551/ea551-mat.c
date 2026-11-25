/*
Aluno: Iago Lucini da Silva
RA: 281244
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "ea551-mat-txt-parser.h"

int main(void)
{
    int c = getchar();
    if (c == EOF) return 1;
    ungetc(c, stdin);

    // Tarefa A
    if (c == '[') {          /* stdin = bytes do .txt */
        yyin = stdin;
        // Chama o parser para interpretar a stream de caracteres de entrada como uma matriz na memória RAM
        if (yyparse() != 0 || !ea551_mat) return 1;

        // Impressão de depuração no stderr
        print_matrix(ea551_mat);

        // Declarando as variáveis do número de linhas e colunas
        ptrdiff_t n_linhas  = arrlen(ea551_mat->linhas);
        ptrdiff_t n_colunas;
        if (n_linhas > 0) {
            n_colunas = arrlen(ea551_mat->linhas[0]); 
        } else {
            // Se a matriz está vazia, o número de colunas é 0
            n_colunas = 0; 
        }        
        ea551_dtype tipo = ea551_mat->dtype;

        // Obrigando as variáveis terem o npumero exato de bytes que eu quero
        uint32_t nlinhas_32 = (uint32_t)n_linhas;
        uint32_t ncols_32   = (uint32_t)n_colunas;
        uint8_t  dtype_8    = (uint8_t)tipo;

        // Gravam o cabeçalho do .emat na ordem: número de linhas, colunas e o dado de maior tipo
        write(STDOUT_FILENO, &nlinhas_32, sizeof(uint32_t));
        write(STDOUT_FILENO, &ncols_32,   sizeof(uint32_t));
        write(STDOUT_FILENO, &dtype_8,    sizeof(uint8_t));

        // Grava os dados conforme o tipo de dados
        for (uint32_t i = 0; i < nlinhas_32; i++) {
            for (uint32_t j = 0; j < ncols_32; j++) {
                double val = ea551_mat->linhas[i][j];

                if (tipo == EA551_DTYPE_UINT8) {
                    uint8_t v = (uint8_t)val;
                    write(STDOUT_FILENO, &v, sizeof(uint8_t));
                } else if (tipo == EA551_DTYPE_INT32) {
                    int32_t v = (int32_t)val;
                    write(STDOUT_FILENO, &v, sizeof(int32_t));
                } else if (tipo == EA551_DTYPE_FLOAT32) {
                    float v = (float)val;
                    write(STDOUT_FILENO, &v, sizeof(float));
                }
            }
        }

        ea551_mat_free(ea551_mat);
        return 0;

    } 
    // Tarefa B
    else {


        uint8_t header_buf[9];
        // Guarda manualmente o byte que o getchar pegou anteriormente
        // Evitar que o arquivo se corrompa
        header_buf[0] = (uint8_t)getchar();

        // Le o restantes dos bytes
        if (read(STDIN_FILENO, &header_buf[1], 8) != 8) {
            return 1;
        }

        uint32_t nlinhas, ncols;
        uint8_t dtype;

        // Pega os dados em bytes do cabeçalho do .emat e coloca em variáveis: nlinha, ncols e dtype
        memcpy(&nlinhas, &header_buf[0], sizeof(uint32_t));
        memcpy(&ncols,   &header_buf[4], sizeof(uint32_t));
        dtype = header_buf[8];

        // Imprime a matriz formatada em texto no stdout
        printf("[\n");
        for (uint32_t i = 0; i < nlinhas; i++) {
            printf("  [");
            for (uint32_t j = 0; j < ncols; j++) {

                if (dtype == EA551_DTYPE_UINT8) {
                    uint8_t v;
                    if (read(STDIN_FILENO, &v, sizeof(uint8_t)) != sizeof(uint8_t)) return 1;
                    printf("%u", v);
                } else if (dtype == EA551_DTYPE_INT32) {
                    int32_t v;
                    if (read(STDIN_FILENO, &v, sizeof(int32_t)) != sizeof(int32_t)) return 1;
                    printf("%d", v);
                } else if (dtype == EA551_DTYPE_FLOAT32) {
                    float v;
                    if (read(STDIN_FILENO, &v, sizeof(float)) != sizeof(float)) return 1;
                    printf("%g", v);
                }

                if (j + 1 < ncols) {
                    printf(", ");
                }
            }
            printf("]");

            if (i + 1 < nlinhas) {
                printf(",\n");
            }
        }
        printf("\n]\n");

        return 0;
    }

    return 1;
}