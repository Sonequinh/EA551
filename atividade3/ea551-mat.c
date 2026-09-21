/* Nesta atividade, vamos criar um formato de arquivo para representação de matrizes chamado ea551-mat (.emat)

Arquivos no Linux são geralmente sequências de bytes.
Essas sequências podem ser interpretadas como uma sequência de caracteres textuais, ou
simplesmente mantidas como um arquivo binário, onde a interpretação é deixada para o código do
usuário.

Você irá criar um formato de arquivo binário para representar matrizes de inteiros ou ponto flutuante:
EA551-MAT (.emat)

Representações de matrizes em .txt são fornecidas como exemplo (arquivos ea551-mat-teste*.txt).

Para ler essas representações de matrizes em .txt, o parser já foi implementado usando Flex e Bison,
com uma estratégia similar ao lab anterior. O parser está em ea551-mat-txt-parser.l e
ea551-mat-txt-parser.y, consulte as definições para mais detalhes.

Para esta atividade, o código fornecido já inclui o .h do parser (ea551-mat-txt-parser.h) e utiliza o
yyparse() para ler a matriz do arquivo de texto para a memória RAM. O parser popula uma variável na
RAM com a matriz representada por arrays dinâmicos de arrays dinâmicos de tipo double. Além disso, o
parser detecta qual o maior tipo necessário para representar a matriz dentre uint8, int32 e float32, e o
armazena em ea551_mat->dtype.

A sua tarefa é implementar a escrita e leitura do arquivo binário .emat, com a seguinte interface:
1) Se a stdin for um .txt (conteúdo da matriz, começa com '['), o parser é usado para interpretar essa
stream como uma matriz na memória RAM, e você deve escrever o arquivo .emat usando stdout.
2) Se a stdin for um .emat (binário), leia as informações de cabeçalho para interpretar o conteúdo do
arquivo e imprima a matriz no terminal.

Utilize os arquivos ea551-mat-teste*.txt para testar a implementação.

Exemplos de uso:
$ ./ea551-mat < ea551-mat-teste1.txt > ea551-mat-teste1.emat
$ ./ea551-mat < ea551-mat-teste1.emat

Baseado na sua implementação, entregue:

* (50%) Código completo e funcional
* (30%) Um relatório com a descrição de como o programa funciona.
* (20%) No seu relatório, demonstre qual é o tamanho e quantos blocos os arquivos .emat estão
  utilizando, em comparação com o tamanho dos arquivos de teste .txt originais. Comente sobre a
  eficiência da sua implementação em comparação com a representação textual.

Utilize as ferramentas "xxd", "ls -ls" e "filefrag -v" para ajudar a debugar e embasar seus comentários.
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

    if (c == '[') {          /* stdin = bytes do .txt */
        yyin = stdin;
        // Chama o parser para interpretar a stream de caracteres de entrada como uma matriz na memória RAM
        // Note que ea551_mat é definido em ea551-mat-txt-parser.h
        if (yyparse() != 0 || !ea551_mat) return 1;

        // Chegamos aqui se o parser rodou sem erros e ea551_mat é um ponteiro válido para a matriz na memória RAM
        print_matrix(ea551_mat);

        /* TODO: gravar ea551_mat em stdout como .emat
           (header com dtype / nlinhas / ncols + sequência de bytes no tipo de ea551_mat->dtype) */

        ea551_mat_free(ea551_mat);
        return 0;
    }

    /* TODO: stdin = bytes do .emat — ler header + sequência de bytes no tipo de ea551_mat->dtype e imprimir a matriz */
    return 1;
}

