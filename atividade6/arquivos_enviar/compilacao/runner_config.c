#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "config_struct.h"
#include "../libmnist.h"

extern FILE *yyin;
extern int yyparse(void);

void inicializar_defaults(void) {
    strcpy(g_config.modo, "");
    strcpy(g_config.caminho_pesos, "");
    strcpy(g_config.caminho_entrada, "");
    g_config.matmul_threads = 1;
    g_config.produtor_threads = 1;
    g_config.produtor_buffer_size = 2048;
    g_config.visualizacao = 0;
}

int validar_configuracoes(void) {
    if (strlen(g_config.modo) == 0) {
        fprintf(stderr, "Erro de Validação: O campo 'modo' é obrigatório (infer ou test).\n");
        return -1;
    }
    if (strcmp(g_config.modo, "infer") != 0 && strcmp(g_config.modo, "test") != 0) {
        fprintf(stderr, "Erro de Validação: Modo inválido '%s'. Use 'infer' ou 'test'.\n", g_config.modo);
        return -1;
    }
    if (access(g_config.caminho_pesos, F_OK) != 0) {
        fprintf(stderr, "Erro de Validação: Arquivo de pesos '%s' não encontrado.\n", g_config.caminho_pesos);
        return -1;
    }
    if (access(g_config.caminho_entrada, F_OK) != 0) {
        fprintf(stderr, "Erro de Validação: Caminho de entrada '%s' não existe.\n", g_config.caminho_entrada);
        return -1;
    }
    if (g_config.matmul_threads <= 0 || g_config.produtor_threads <= 0) {
        fprintf(stderr, "Erro de Validação: Número de threads deve ser positivo (> 0).\n");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Uso: %s <arquivo_config.yaml>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "r");
    if (!f) {
        perror("Erro ao abrir arquivo de configuração");
        return 1;
    }

    inicializar_defaults();
    yyin = f;

    if (yyparse() != 0) {
        fprintf(stderr, "Falha no parse do arquivo de configuração EA551-YAML.\n");
        fclose(f);
        return 1;
    }
    fclose(f);

    if (validar_configuracoes() != 0) {
        fprintf(stderr, "Execução interrompida devido a parâmetros inválidos.\n");
        return 1;
    }

    printf("================ CONFIGURAÇÃO PARSEADA COM SUCESSO ================\n");
    printf("Modo               : %s\n", g_config.modo);
    printf("Caminho Pesos      : %s\n", g_config.caminho_pesos);
    printf("Caminho Entrada    : %s\n", g_config.caminho_entrada);
    printf("Threads (Produtor) : %d\n", g_config.produtor_threads);
    printf("Threads (MatMul)   : %d\n", g_config.matmul_threads);
    printf("Visualização X11   : %s\n", g_config.visualizacao ? "LIGADO" : "DESLIGADO");
    printf("===================================================================\n\n");

    // Execução dinâmica dos módulos da libmnist.so
    if (strcmp(g_config.modo, "infer") == 0) {
        return executar_inferencia_c(g_config.caminho_entrada, g_config.caminho_pesos);
    } else if (strcmp(g_config.modo, "test") == 0) {
        return executar_modo_teste_c(g_config.caminho_entrada, g_config.produtor_threads, g_config.caminho_pesos);
    }

    return 0;
}