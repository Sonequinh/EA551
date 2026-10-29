#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>     // Para mmap() (visto na prática presencial g)
#include <semaphore.h>    // Para semáforos POSIX sem_open/sem_wait/sem_post
#include <sys/wait.h>     // Para waitpid()
#include <signal.h>       // Para sigaction() e tratamento do SIGINT
#include "fenster.h"

// Reaproveita o cabeçalho empacotado da Atividade 3
typedef struct __attribute__((packed)) {
    uint32_t linhas;   // 4 bytes
    uint32_t colunas;  // 4 bytes
    uint8_t  dtype;    // 1 byte
} HeaderEmat;

// Estrutura mapeada na memória compartilhada
typedef struct {
    HeaderEmat header;
    uint8_t pixels[1024 * 1024]; // Tamanho suficiente para os pixels da matriz
} MemoriaCompartilhada;

// Ponteiros globais ou estáticos para limpeza graciosa no manipulador de sinais
MemoriaCompartilhada *shm = NULL;       // guarda o endereço do bloco de memória partilhada reservado via mmap()
sem_t *sem = NULL;                      // guarda o endereço do semáforo
pid_t pid_filho = 0;                    // pid do filho

// Handler para o sinal SIGINT (Ctrl+C)
void tratar_sigint(int sig) {
    // Se formos o processo pai, avisamos/esperamos o filho e limpamos recursos
    (void)sig;
    if (pid_filho > 0) {
        // Envia sinal para o filho terminar graciosamente (sem usar SIGKILL)
        kill(pid_filho, SIGINT); 
        wait(NULL); // Aguarda término para evitar processo zumbi
    }
    
    // Desfaz o mapeamento da memória e fecha o semáforo
    munmap(shm, sizeof(MemoriaCompartilhada));
    sem_close(sem);
    sem_unlink("/sem_emat");
    exit(0);
}

int main(int argc, char *argv[]) {
    (void)argc;
    // PASSAGEM 1: Configurar tratamento de sinais (SIGINT / Ctrl+C)
    // Usa sigaction conforme visto na Prática Presencial (exercício e/f)
    struct sigaction sa = {0};
    sa.sa_handler = tratar_sigint;
    sigaction(SIGINT, &sa, NULL);

    // PASSAGEM 2: Criar Semáforo Nomeado (Inicializado em 0 -> bloqueado)
    // Usando sem_open conforme visto no exercício (g) da Prática Presencial
    sem = sem_open("/sem_emat", O_CREAT, 0600, 0);

    // PASSAGEM 3: Criar a Memória Compartilhada com mmap
    // Flags MAP_SHARED e MAP_ANONYMOUS garantem visibilidade entre pai e filho
    shm = mmap(NULL, sizeof(MemoriaCompartilhada), 
               PROT_READ | PROT_WRITE, 
               MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    // PASSAGEM 4: Criar o processo filho com fork()
    pid_filho = fork();

    if (pid_filho == 0) {
        // 1. Aguarda autorização do Pai (Operação DOWN / sem_wait)
        sem_wait(sem);

        // 2. Lê as dimensões registradas na memória compartilhada
        uint32_t larg = shm->header.colunas;
        uint32_t alt  = shm->header.linhas;

        // Fator de escala para multiplicar os pixels da imagem (ex: 28x28 -> 448x448)
        const int ESCALA = 16;
        const int janela_largura = larg * ESCALA;
        const int janela_altura  = alt * ESCALA;

        // Aloca o buffer zerado usando calloc (igual ao template fenster.c)
        uint32_t *buf_fenster = calloc((size_t)janela_largura * janela_altura, sizeof(*buf_fenster));
        if (!buf_fenster) {
            exit(1);
        }

        // Confere se há Display X11 ativo (conforme template fenster.c)
        Display *dpy = XOpenDisplay(NULL);
        if (!dpy) {
            fprintf(stderr, "Erro: Não foi possível abrir o display X11.\n");
            free(buf_fenster);
            exit(1);
        }
        XCloseDisplay(dpy);

        // Configura a estrutura da janela
        struct fenster f = {
            .title = "Visualizador EMAT",
            .width = janela_largura,
            .height = janela_altura,
            .buf = buf_fenster
        };

        // Prepara e pinta os pixels aplicando a escala (igual à função pinta do template)
        for (uint32_t i = 0; i < alt; i++) {
            for (uint32_t j = 0; j < larg; j++) {
                uint8_t v = shm->pixels[i * larg + j];

                // Formato de cor ARGB 0x00RRGGBB
                uint32_t cor = ((uint32_t)v << 16) | ((uint32_t)v << 8) | (uint32_t)v;

                // Pinta um quadrado de ESCALA x ESCALA pixels para cada ponto da matriz
                for (int dy = 0; dy < ESCALA; dy++) {
                    for (int dx = 0; dx < ESCALA; dx++) {
                        fenster_pixel(&f, j * ESCALA + dx, i * ESCALA + dy) = cor;
                    }
                }
            }
        }

        // Tenta abrir a janela com tratamento de erro
        if (fenster_open(&f) != 0) {
            free(buf_fenster);
            exit(1);
        }

        // Loop gráfico de exibição com pausa de 16ms (~60 FPS)
        while (fenster_loop(&f) == 0) {
            fenster_sleep(16);
        }

        // Encerramento certo
        fenster_close(&f);
        free(buf_fenster);

        exit(0);      

    } else {

        // Abre o arquivo .emat recebido via argumento
        int fd = open(argv[1], O_RDONLY);

        // Lê os 9 bytes do cabeçalho via syscall read()
        read(fd, &(shm->header), sizeof(HeaderEmat));

        // Lê o restante do arquivo (pixels) gravando na memória compartilhada
        size_t total_pixels = shm->header.linhas * shm->header.colunas;
        read(fd, shm->pixels, total_pixels);

        close(fd);

        // Notifica o filho de que os dados estão prontos (Operação UP / sem_post)
        sem_post(sem);

        // Aguarda o filho encerrar para não deixar zumbi
        int estado;
        waitpid(pid_filho, &estado, 0);

        // Limpeza final dos recursos
        munmap(shm, sizeof(MemoriaCompartilhada));
        sem_close(sem);
        sem_unlink("/sem_emat");
    }

    return 0;
}