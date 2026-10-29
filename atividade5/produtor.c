#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>

#define SHM_NAME        "/shm_mnist"
#define SEM_EMPTY_NAME  "/sem_mnist_empty"
#define SEM_FULL_NAME   "/sem_mnist_full"
#define SEM_MUTEX_NAME  "/sem_mnist_mutex"

#define NUM_SLOTS       2048    // Capacidade máxima do buffer
#define IMG_BYTES       784     // 28 x 28

// Cabeçalho binário .emat (9 bytes)
typedef struct __attribute__((packed)) {
    uint32_t linhas;
    uint32_t colunas;
    uint8_t  dtype;
} HeaderEmat;

// Slot do buffer circular (788 bytes)
typedef struct {
    uint8_t image[IMG_BYTES];
    int32_t label;
} SampleSlot;

// Layout completo mapeado na Memória Compartilhada
typedef struct {
    uint32_t total_samples;
    uint32_t head;
    uint32_t tail;
    SampleSlot slots[NUM_SLOTS];
} SharedBuffer;

// Estrutura para fila de tarefas dos arquivos
typedef struct {
    char **paths;
    int *labels;
    int total;
    int current_idx;
    pthread_mutex_t queue_mutex;
} WorkQueue;

// Variáveis Globais
SharedBuffer *shm = NULL;
sem_t *sem_empty = NULL;
sem_t *sem_full = NULL;
sem_t *sem_mutex = NULL;
int shm_fd = -1;
WorkQueue queue;

// Função para desalocar todos os recursos
void limpar_recursos(void) {
    if (shm && shm != MAP_FAILED) munmap(shm, sizeof(SharedBuffer));
    if (shm_fd >= 0) close(shm_fd);
    if (sem_empty) sem_close(sem_empty);
    if (sem_full) sem_close(sem_full);
    if (sem_mutex) sem_close(sem_mutex);

    shm_unlink(SHM_NAME);
    sem_unlink(SEM_EMPTY_NAME);
    sem_unlink(SEM_FULL_NAME);
    sem_unlink(SEM_MUTEX_NAME);
}

// Ao pressionar CTRL+C aciona um sinal
void tratar_sigint(int sig) {
    (void)sig;
    limpar_recursos();
    exit(0);
}

// Indexador recursivo de arquivos .emat
void indexar_diretorio(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                indexar_diretorio(full_path);
            } else if (S_ISREG(st.st_mode) && strstr(entry->d_name, ".emat")) {
                // Extrai o rótulo do nome da pasta pai (ex: .../0/60001.emat -> 0)
                char path_copy[1024];
                strncpy(path_copy, full_path, sizeof(path_copy));
                char *last_slash = strrchr(path_copy, '/');
                int label = 0;
                if (last_slash) {
                    *last_slash = '\0';
                    char *prev_slash = strrchr(path_copy, '/');
                    label = atoi(prev_slash ? prev_slash + 1 : path_copy);
                }

                // Aloca o espaço dinamico e armazena os dados na fila
                queue.paths = realloc(queue.paths, (queue.total + 1) * sizeof(char *));
                queue.labels = realloc(queue.labels, (queue.total + 1) * sizeof(int));
                queue.paths[queue.total] = strdup(full_path);
                queue.labels[queue.total] = label;
                queue.total++;
            }
        }
    }
    closedir(dir);
}

// Função executada por cada Thread Produtora
void *worker_producer(void *arg) {
    (void)arg;

    while (1) {
        int idx_trabalho = -1;

        // Região crítica 1: retiro o próximo arquivo da fila compartilhada
        pthread_mutex_lock(&queue.queue_mutex);
        if (queue.current_idx < queue.total) {
            idx_trabalho = queue.current_idx++;
        }
        pthread_mutex_unlock(&queue.queue_mutex);

        if (idx_trabalho == -1) break; // Todos os arquivos foram lidos

        const char *filepath = queue.paths[idx_trabalho];
        int label = queue.labels[idx_trabalho];

        // Leitura do arquivo .emat
        int fd = open(filepath, O_RDONLY);
        if (fd < 0) continue;

        HeaderEmat header;
        if (read(fd, &header, sizeof(HeaderEmat)) != sizeof(HeaderEmat)) {
            close(fd);
            continue;
        }

        uint8_t img_buf[IMG_BYTES];
        if (read(fd, img_buf, IMG_BYTES) != IMG_BYTES) {
            close(fd);
            continue;
        }
        close(fd);

        // Região crítica 2: Sincronização e gravação no buffer compartilhado
        sem_wait(sem_empty); // Espera por slot vago
        sem_wait(sem_mutex); // BLoqueia o acesso ao ponteiro 'head'

        uint32_t slot_idx = shm->head;
        shm->head = (shm->head + 1) % NUM_SLOTS;

        // Copia imagem e rótulo direto da memória compartilhada
        memcpy(shm->slots[slot_idx].image, img_buf, IMG_BYTES);
        shm->slots[slot_idx].label = (int32_t)label;

        sem_post(sem_mutex);    // Libera o acesso ao ponteiro 'head'
        sem_post(sem_full);     // Notifica o consumidor Python que há dados prontos
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <caminho_mnist_emat_train> [num_threads]\n", argv[0]);
        return 1;
    }

    const char *dir_treino = argv[1];
    int num_threads = (argc >= 3) ? atoi(argv[2]) : 1;
    if (num_threads < 1) num_threads = 1;

    // Tratamento do CTRL+C
    struct sigaction sa = {0};
    sa.sa_handler = tratar_sigint;
    sigaction(SIGINT, &sa, NULL);

    limpar_recursos(); // Garante estado limpo antes de iniciar

    printf("Indexando arquivos .emat em %s...\n", dir_treino);
    queue.paths = NULL;
    queue.labels = NULL;
    queue.total = 0;
    queue.current_idx = 0;
    pthread_mutex_init(&queue.queue_mutex, NULL);

    indexar_diretorio(dir_treino);
    printf("Total de amostras encontradas: %d\n", queue.total);

    if (queue.total == 0) {
        fprintf(stderr, "Nenhum arquivo .emat encontrado!\n");
        return 1;
    }

    // Inicializando os semáforos
    sem_empty = sem_open(SEM_EMPTY_NAME, O_CREAT, 0666, NUM_SLOTS);
    sem_full  = sem_open(SEM_FULL_NAME, O_CREAT, 0666, 0);
    sem_mutex = sem_open(SEM_MUTEX_NAME, O_CREAT, 0666, 1);

    // Inicializando a memória compartilhada
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(SharedBuffer));
    shm = mmap(NULL, sizeof(SharedBuffer), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    // Inicializando variáveis do buffer compartilhado
    shm->total_samples = queue.total;
    shm->head = 0;
    shm->tail = 0;

    printf("Iniciando produtor em C (%d thread(s))...\n", num_threads);

    // Criação das Threads produtoras
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, worker_producer, NULL);
    }

    // Aguarda todas as Threads terminarem a leitura
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("Produtor em C concluiu o carregamento de todas as amostras!\n");

    // LIbera a memória local
    free(threads);
    for (int i = 0; i < queue.total; i++) free(queue.paths[i]);
    free(queue.paths);
    free(queue.labels);

    return 0;
}