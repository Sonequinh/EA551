%{

#include <stdio.h>
#include "exemplo.tab.h"    /* definicao das classes de itens lexicos */
extern int yylval;

%}

%%

[01]+   {sscanf (yytext, "%d", &yylval); 
         return INT; }       /* valores 0 e 1 com repeticao */ 

\n      {return NL; }        /* line feed */

.       {return INVALIDO; }  /* qualquer outro caracter, exceto line feed */

%%
/* não precisamos de rotinas do usuário se vamos linkar com o executável do bison. */