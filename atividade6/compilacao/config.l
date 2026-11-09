%{
#include <stdio.h>
#include <string.h>
#include "config.tab.h"
%}

%option noyywrap
%option yylineno
%option nounput noinput

%%

"#"[^\n]*                             { /* Ignora comentários */ }
[ \t]+                                { /* Ignora espaços e tabs */ }
\r?\n                                 { return NL; }

"modo"                                { return KEY_MODO; }
"caminho_pesos"                       { return KEY_PESOS; }
"caminho_entrada"                     { return KEY_ENTRADA; }
"matmul_threads"                      { return KEY_MATMUL_THREADS; }
"produtor_threads"                    { return KEY_PRODUTOR_THREADS; }
"produtor_buffer"                     { return KEY_BUFFER_SIZE; }
"visualizacao"                        { return KEY_VISUALIZACAO; }

":"                                   { return COLON; }

[0-9]+                                { yylval.ival = atoi(yytext); return INT_VAL; }
[tT][rR][uU][eE]|[oO][nN]             { yylval.ival = 1; return BOOL_VAL; }
[fF][aA][lL][sS][eE]|[oO][fF][fF]     { yylval.ival = 0; return BOOL_VAL; }

\"[^\"]*\"                            { 
                                        // Remove as aspas da string
                                        yytext[strlen(yytext)-1] = '\0';
                                        yylval.sval = strdup(yytext + 1); 
                                        return STRING_VAL; 
                                      }
[a-zA-Z0-9_\.\/-]+                    { yylval.sval = strdup(yytext); return STRING_VAL; }

.                                     { return INVALIDO; }

%%