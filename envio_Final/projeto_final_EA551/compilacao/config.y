%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config_struct.h"

int yylex(void);
AppConfig g_config;

%}

%union {
    int ival;
    char *sval;
}

%token KEY_MODO KEY_PESOS KEY_ENTRADA KEY_MATMUL_THREADS KEY_PRODUTOR_THREADS KEY_CONSUMIDORES KEY_BUFFER_SIZE KEY_VISUALIZACAO
%token COLON NL INVALIDO
%token <ival> INT_VAL BOOL_VAL
%token <sval> STRING_VAL

%%

config_file : lines
            ;

lines       : line
            | lines NL line
            ;

line        : /* vazio */
            | statement
            ;

statement   : KEY_MODO COLON STRING_VAL 
                  { snprintf(g_config.modo, sizeof(g_config.modo), "%s", $3); free($3); }
            | KEY_PESOS COLON STRING_VAL 
                  { snprintf(g_config.caminho_pesos, sizeof(g_config.caminho_pesos), "%s", $3); free($3); }
            | KEY_ENTRADA COLON STRING_VAL 
                  { snprintf(g_config.caminho_entrada, sizeof(g_config.caminho_entrada), "%s", $3); free($3); }
            | KEY_MATMUL_THREADS COLON INT_VAL 
                  { g_config.matmul_threads = $3; }
            | KEY_MATMUL_THREADS COLON BOOL_VAL 
                  { g_config.matmul_threads = $3; }
            | KEY_PRODUTOR_THREADS COLON INT_VAL 
                  { g_config.produtor_threads = $3; }
            | KEY_PRODUTOR_THREADS COLON BOOL_VAL 
                  { g_config.produtor_threads = $3; }
            | KEY_CONSUMIDORES COLON INT_VAL 
                  { g_config.consumidores = $3; }
            | KEY_BUFFER_SIZE COLON INT_VAL 
                  { g_config.produtor_buffer_size = $3; }
            | KEY_VISUALIZACAO COLON BOOL_VAL 
                  { g_config.visualizacao = $3; }
            | KEY_VISUALIZACAO COLON INT_VAL 
                  { g_config.visualizacao = $3; }
            | KEY_VISUALIZACAO COLON STRING_VAL 
            { 
            if (strcasecmp($3, "true") == 0 || strcasecmp($3, "on") == 0 || strcmp($3, "1") == 0)
                  g_config.visualizacao = 1;
            else
                  g_config.visualizacao = 0;
            free($3); 
            }
            ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Erro de sintaxe na linha %d: %s\n", yylineno, s);
}