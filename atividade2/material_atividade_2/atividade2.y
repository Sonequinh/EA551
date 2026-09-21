// analisador sintático Bison 

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int yylex(void);
int yyerror(char const *s);

/* 
Vetor para as variaveis iniciadas
Contador
*/
char *vars_liberadas[200];
int total_liberadas = 0;

/*

*/
void marcar_iniciada(const char *nome) {
    for (int i = 0; i < total_liberadas; i++) {
        if (strcmp(vars_liberadas[i], nome) == 0) return;
    }
    vars_liberadas[total_liberadas++] = strdup(nome);
}

/*

*/
void verificar_acesso(const char *nome) {
    for (int i = 0; i < total_liberadas; i++) {
        if (strcmp(vars_liberadas[i], nome) == 0) return;
    }
    printf("Variável não inicializada: '%s'\n", nome);
}
%}


%union {
    char *str;
}

// Declaração dos tokens
%token TIPO INICIO_MAIN ESQ_CHAVE DIR_CHAVE                                    // Palavras chave
%token IGUAL PONTO_VIRGULA VIRGULA PARENTESES_ESQ PARENTESES_DIR OPERA VALOR    // Operadores, pontuações e valores
%token <str> NOMEVAR                                                            // Nome da variável associado ao %union

%left OPERA

%%

//
prog_fonte      : cabecalho ESQ_CHAVE conteudo_prog DIR_CHAVE
                | conteudo_prog
                ;

// 
cabecalho       : TIPO INICIO_MAIN PARENTESES_ESQ PARENTESES_DIR
                | INICIO_MAIN PARENTESES_ESQ PARENTESES_DIR
                ;

conteudo_prog   : declaracoes expressoes
                | declaracoes
                | expressoes
                ;

declaracoes     : linha_declara declaracoes
                | linha_declara
                ;

linha_declara   : TIPO variaveis PONTO_VIRGULA
                ;

variaveis       : identificador VIRGULA variaveis
                | identificador
                ;

identificador   : NOMEVAR
                | NOMEVAR IGUAL operacoes { 
                    marcar_iniciada($1); 
                    free($1); 
                  }
                ;

expressoes      : linha_executavel expressoes
                | linha_executavel
                ;

linha_executavel: NOMEVAR IGUAL operacoes PONTO_VIRGULA { 
                    marcar_iniciada($1); 
                    free($1); 
                  }
                ;

operacoes       : operacoes OPERA operacoes
                | PARENTESES_ESQ operacoes PARENTESES_DIR
                | VALOR
                | NOMEVAR { 
                    verificar_acesso($1); 
                    free($1); 
                  }
                ;

%%

int main() {
    yyparse();
    return 0;
}

int yyerror(char const *s) {
    fprintf(stderr, "%s\n", s);
    return 0;
}