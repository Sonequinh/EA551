/*
Aluno: Iago Lucini da Silva
RA: 281244
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ea551-mat-txt-parser.h"



int main(void)
{
    int c = getchar();
    if (c == EOF) return 1;
    ungetc(c, stdin);

    //  
    if (c == '[') {          /* stdin = bytes do .txt */
        yyin = stdin;
        // Chama o parser para interpretar a stream de caracteres de entrada como uma matriz na memória RAM
        // Note que ea551_mat é definido em ea551-mat-txt-parser.h
        if (yyparse() != 0 || !ea551_mat) return 1;

        // Chegamos aqui se o parser rodou sem erros e ea551_mat é um ponteiro válido para a matriz na memória RAM
        print_matrix(ea551_mat);

        /* TODO: gravar ea551_mat em stdout como .emat
           (header com dtype / nlinhas / ncols + sequência de bytes no tipo de ea551_mat->dtype) */


        // Verificar número de linhas e colunas
        ptrdiff_t n_linhas = arrlen(ea551->linhas);
        ptrdiff_t n_colunas = n_linhas ? arrlen(ea551->linhas[0]) : 0;
        
        // Verificar o elemento de maior tipo: EA551_DTYPE_UINT8 < EA551_DTYPE_INT32 < EA551_DTYPE_FLOAT32
        ea551_dtype tipo = ea551_mat->dtype;

        ea551_mat_free(ea551_mat);
        return 0;
    } else {


    }

    return 1;
}

