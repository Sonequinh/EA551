#ifndef CONFIG_STRUCT_H
#define CONFIG_STRUCT_H

typedef struct {
    char modo[16];                // "infer" ou "test"
    char caminho_pesos[256];      // ex: "./W.emat"
    char caminho_entrada[256];    // ex: "imagem.emat" ou "./mnist_emat/test"
    int matmul_threads;         // ex: 4
    int produtor_threads;       // ex: 4
    int produtor_buffer_size;   // ex: 2048
    int visualizacao;           // 1 (on) ou 0 (off)
} AppConfig;

extern AppConfig g_config;
extern int yylineno;
void yyerror(const char *s);

#endif