%{
/* Definicoes */
#include <stdio.h>
#include <stdlib.h>
%}

%%

[01]+   { printf("INT: %s (valor=%d)\n", yytext, atoi(yytext)); return 1; }
\n      { printf("NL\n"); return 1; }
.       { printf("INVALIDO: '%s'\n", yytext); return 1; }


