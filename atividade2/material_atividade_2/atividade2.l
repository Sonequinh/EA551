

%{
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include "atividade2.tab.h"
%}

%%

"int"|"float"|"char"|"double"   { return TIPO; }
"main"                          { return INICIO_MAIN; }
"{"                             { return ESQ_CHAVE; }
"}"                             { return DIR_CHAVE; }
"="                             { return IGUAL; }
";"                             { return PONTO_VIRGULA; }
","                             { return VIRGULA; }
"("                             { return PARENTESES_ESQ; }
")"                             { return PARENTESES_DIR; }
"+"|"-"|"*"|"/"                 { return OPERA; }

[0-9]+(\.[0-9]+)?               { return VALOR; }

[a-zA-Z_][a-zA-Z0-9_]*          { 
                                    yylval.str = strdup(yytext); 
                                    return NOMEVAR; 
                                }


[ \t\n\r]+                      {/* Sem retorno -> ignora essas condições */}
"//".*                          {/* Sem retorno -> ignora essas condições */}

.                               {/* Sem retorno -> ignora essas condições */}

%%