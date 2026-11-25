CC = gcc
CFLAGS = -O3 -Wall -I. -I./compilacao
LDFLAGS = -L. -Wl,-rpath,. -lmnist -lpthread -lrt -lm -lX11

all: libmnist.so runner_yaml

libmnist.so: produtor.c matmul.c visualizador.c libmnist.h fenster.h
	$(CC) -shared -fPIC -O3 -Wall -I. produtor.c matmul.c visualizador.c -o libmnist.so -lpthread -lrt -lm -lX11

compilacao/config.tab.c compilacao/config.tab.h: compilacao/config.y
	bison -d compilacao/config.y -o compilacao/config.tab.c

compilacao/lex.yy.c: compilacao/config.l compilacao/config.tab.h
	flex -o compilacao/lex.yy.c compilacao/config.l

runner_yaml: compilacao/lex.yy.c compilacao/config.tab.c compilacao/runner_config.c libmnist.so
	$(CC) $(CFLAGS) compilacao/lex.yy.c compilacao/config.tab.c compilacao/runner_config.c -o runner_yaml $(LDFLAGS)

clean:
	rm -f compilacao/lex.yy.c compilacao/config.tab.c compilacao/config.tab.h libmnist.so runner_yaml