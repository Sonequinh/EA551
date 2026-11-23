CC = gcc
CFLAGS = -O3 -Wall -I. -I./compilacao
LIBS = -L. -Wl,-rpath,. -lmnist -lpthread -lrt -lm

all: libmnist.so runner_yaml

# Regras do Bison e Flex gerando arquivos na pasta compilacao
compilacao/config.tab.c compilacao/config.tab.h: compilacao/config.y
	bison -d compilacao/config.y -o compilacao/config.tab.c

compilacao/lex.yy.c: compilacao/config.l compilacao/config.tab.h
	flex -o compilacao/lex.yy.c compilacao/config.l

# Gerando o executável final na raiz do projeto
runner_yaml: compilacao/lex.yy.c compilacao/config.tab.c compilacao/runner_config.c libmnist.so
	$(CC) $(CFLAGS) compilacao/lex.yy.c compilacao/config.tab.c compilacao/runner_config.c -o runner_yaml $(LIBS)

clean:
	rm -f compilacao/lex.yy.c compilacao/config.tab.c compilacao/config.tab.h runner_yaml