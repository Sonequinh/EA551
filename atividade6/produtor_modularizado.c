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
#include <time.h>
#include "libmnist.h"

typedef struct {
    char **paths;
    int *labels;
    int total;
    int current_idx;
    pthread_mutex_t queue_mutex;
} WorkQueue;

// Variáveis Globais internas do módulo
static SharedBuffer *shm = NULL;
static sem_t *sem_empty = NULL;
static sem_t *sem_full = NULL;
static sem_t *sem_mutex = NULL;
static int shm_fd = -1;
static WorkQueue queue;

static pthread_t *threads_handles = NULL;
static int g_num_threads = 0;

void limpar_recursos(void) {
    if (shm && shm != MAP_FAILED) {
        munmap(shm, sizeof(SharedBuffer));
        shm = NULL;
    }
    if (shm_fd >= 0) {
        close(shm_fd);
        shm_fd = -1;
    }
    if (sem_empty) { sem_close(sem_empty); sem_empty = NULL; }
    if (sem_full)  { sem_close(sem_full);  sem_full = NULL;  }
    if (sem_mutex) { sem_close(sem_mutex); sem_mutex = NULL; }

    shm_unlink(SHM_NAME);
    sem_unlink(SEM_EMPTY_NAME);
    sem_unlink(SEM_FULL_NAME);
    sem_unlink(SEM_MUTEX_NAME);
}

static void tratar_sigint(int sig) {
    (void)sig;
    limpar_recursos();
    exit(0);
}

static void indexar_diretorio(const char *dir_path) {
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
                char path_copy[1024];
                strncpy(path_copy, full_path, sizeof(path_copy));
                char *last_slash = strrchr(path_copy, '/');
                int label = 0;
                if (last_slash) {
                    *last_slash = '\0';
                    char *prev_slash = strrchr(path_copy, '/');
                    label = atoi(prev_slash ? prev_slash + 1 : path_copy);
                }

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

static void embaralhar_fila(void) {
    srand(time(NULL));
    for (int i = queue.total - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        
        char *temp_path = queue.paths[i];
        queue.paths[i] = queue.paths[j];
        queue.paths[j] = temp_path;

        int temp_label = queue.labels[i];
        queue.labels[i] = queue.labels[j];
        queue.labels[j] = temp_label;
    }
}

static void *worker_producer(void *arg) {
    (void)arg;

    while (1) {
        int idx_trabalho = -1;

        pthread_mutex_lock(&queue.queue_mutex);
        if (queue.current_idx < queue.total) {
            idx_trabalho = queue.current_idx++;
        }
        pthread_mutex_unlock(&queue.queue_mutex);

        if (idx_trabalho == -1) break;

        const char *filepath = queue.paths[idx_trabalho];
        int label = queue.labels[idx_trabalho];

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

        sem_wait(sem_empty);
        sem_wait(sem_mutex);

        uint32_t slot_idx = shm->head;
        shm->head = (shm->head + 1) % NUM_SLOTS;

        memcpy(shm->slots[slot_idx].image, img_buf, IMG_BYTES);
        shm->slots[slot_idx].label = (int32_t)label;

        sem_post(sem_mutex);
        sem_post(sem_full);
    }

    return NULL;
}

int iniciar_produtor(const char *dir_treino, int num_threads) {
    if (num_threads < 1) num_threads = 1;
    g_num_threads = num_threads;

    struct sigaction sa = {0};
    sa.sa_handler = tratar_sigint;
    sigaction(SIGINT, &sa, NULL);

    limpar_recursos();

    queue.paths = NULL;
    queue.labels = NULL;
    queue.total = 0;
    queue.current_idx = 0;
    pthread_mutex_init(&queue.queue_mutex, NULL);

    indexar_diretorio(dir_treino);
    if (queue.total == 0) {
        fprintf(stderr, "Erro: Nenhum arquivo .emat encontrado em %s\n", dir_treino);
        return -1;
    }

    embaralhar_fila();

    sem_empty = sem_open(SEM_EMPTY_NAME, O_CREAT, 0666, NUM_SLOTS);
    sem_full  = sem_open(SEM_FULL_NAME, O_CREAT, 0666, 0);
    sem_mutex = sem_open(SEM_MUTEX_NAME, O_CREAT, 0666, 1);

    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("Erro ao executar shm_open");
        return -1;
    }
    ftruncate(shm_fd, sizeof(SharedBuffer));
    shm = mmap(NULL, sizeof(SharedBuffer), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    shm->total_samples = queue.total;
    shm->head = 0;
    shm->tail = 0;

    threads_handles = malloc(g_num_threads * sizeof(pthread_t));
    for (int i = 0; i < g_num_threads; i++) {
        pthread_create(&threads_handles[i], NULL, worker_producer, NULL);
    }

    return queue.total; // Retorna a quantidade de amostras carregadas
}

void finalizar_produtor(void) {
    if (threads_handles) {
        for (int i = 0; i < g_num_threads; i++) {
            pthread_join(threads_handles[i], NULL);
        }
        free(threads_handles);
        threads_handles = NULL;
    }

    if (queue.paths) {
        for (int i = 0; i < queue.total; i++) free(queue.paths[i]);
        free(queue.paths);
        free(queue.labels);
        queue.paths = NULL;
        queue.labels = NULL;
    }

    pthread_mutex_destroy(&queue.queue_mutex);
    limpar_recursos();
}