%{
#include <stdio.h>
#include <stdlib.h>
int yylex(void);
void yyerror(const char *s);
%}

%token INT INVALIDO NL
%%
start   : inteiro NL
             {$$ = $1;
              printf ("valor = %d\n", $$); exit(0); }
        ;
inteiro : inteiro digito 
             {$$ = 10 * $1 + $2; }
          | digito
             {$$ = $1; }
        ;
digito  : INT
             {$$ = $1; }
%%
void yyerror(const char *s)
{
  fprintf(stderr, "Erro de sintaxe! %s\n", s);
}

int main(void)
{
  return yyparse();
}
