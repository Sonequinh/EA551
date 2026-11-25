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
#include <errno.h>
#include <time.h>
#include "libmnist.h"

typedef struct {
    char **paths;
    int *labels;
    int total;
    int current_idx;
    pthread_mutex_t queue_mutex;
} WorkQueue;

static SharedBuffer *shm = NULL;
static uint32_t g_buffer_capacity = 0;
static sem_t *sem_empty = NULL;
static sem_t *sem_full = NULL;
static sem_t *sem_mutex = NULL;
static int shm_fd = -1;
static WorkQueue queue;

static pthread_t *threads_handles = NULL;
static int g_num_threads = 0;
static volatile sig_atomic_t g_stop_produtor = 0;

void limpar_recursos(void) {
    if (shm && shm != MAP_FAILED) {
        size_t map_len = (g_buffer_capacity > 0) ? shm_buffer_bytes(g_buffer_capacity) : sizeof(SharedBuffer);
        munmap(shm, map_len);
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
    g_buffer_capacity = 0;
}

static void tratar_sigint_produtor(int sig) {
    (void)sig;
    g_stop_produtor = 1;
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
                snprintf(path_copy, sizeof(path_copy), "%s", full_path);
                char *last_slash = strrchr(path_copy, '/');
                int label = 0;
                if (last_slash) {
                    *last_slash = '\0';
                    char *prev_slash = strrchr(path_copy, '/');
                    label = atoi(prev_slash ? prev_slash + 1 : path_copy);
                }

                char **new_paths = realloc(queue.paths, (size_t)(queue.total + 1) * sizeof(char *));
                int *new_labels = realloc(queue.labels, (size_t)(queue.total + 1) * sizeof(int));
                if (!new_paths || !new_labels) {
                    perror("Erro de realocação na indexação");
                    closedir(dir);
                    return;
                }
                queue.paths = new_paths;
                queue.labels = new_labels;
                queue.paths[queue.total] = strdup(full_path);
                queue.labels[queue.total] = label;
                queue.total++;
            }
        }
    }
    closedir(dir);
}

static void embaralhar_fila(void) {
    srand((unsigned int)time(NULL));
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

static int ler_tudo(int fd, void *buf, size_t nbytes) {
    uint8_t *p = (uint8_t *)buf;
    size_t total_lido = 0;
    while (total_lido < nbytes) {
        ssize_t n = read(fd, p + total_lido, nbytes - total_lido);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        total_lido += (size_t)n;
    }
    return 0;
}

static void publicar_slot(const uint8_t *img_buf, int32_t label) {
    while (sem_wait(sem_empty) != 0) {
        if (errno != EINTR || g_stop_produtor) return;
    }
    while (sem_wait(sem_mutex) != 0) {
        if (errno != EINTR || g_stop_produtor) {
            sem_post(sem_empty);
            return;
        }
    }

    uint32_t slot_idx = shm->head;
    shm->head = (shm->head + 1) % shm->capacity;

    if (img_buf) {
        memcpy(shm->slots[slot_idx].image, img_buf, IMG_BYTES);
    } else {
        memset(shm->slots[slot_idx].image, 0, IMG_BYTES);
    }
    shm->slots[slot_idx].label = label;

    sem_post(sem_mutex);
    sem_post(sem_full);
}

static void *worker_producer(void *arg) {
    (void)arg;

    while (!g_stop_produtor) {
        int idx_trabalho = -1;

        pthread_mutex_lock(&queue.queue_mutex);
        if (queue.current_idx < queue.total) {
            idx_trabalho = queue.current_idx++;
        }
        pthread_mutex_unlock(&queue.queue_mutex);

        if (idx_trabalho == -1) break;

        const char *filepath = queue.paths[idx_trabalho];
        int32_t label = (int32_t)queue.labels[idx_trabalho];

        uint8_t img_buf[IMG_BYTES];
        int ok = 0;

        int fd = open(filepath, O_RDONLY);
        if (fd >= 0) {
            HeaderEmat header;
            if (ler_tudo(fd, &header, sizeof(HeaderEmat)) == 0 &&
                ler_tudo(fd, img_buf, IMG_BYTES) == 0) {
                ok = 1;
            }
            close(fd);
        }

        if (ok) {
            publicar_slot(img_buf, label);
        } else {
            publicar_slot(NULL, -1);
        }
    }

    return NULL;
}

int iniciar_produtor_ext(const char *dir_treino, int num_threads, int buffer_size) {
    if (num_threads < 1) num_threads = 1;
    if (buffer_size < 1) buffer_size = DEFAULT_BUFFER_SIZE;

    g_num_threads = num_threads;
    g_stop_produtor = 0;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = tratar_sigint_produtor;
    sigaction(SIGINT, &sa, NULL);

    limpar_recursos();
    g_buffer_capacity = (uint32_t)buffer_size;

    queue.paths = NULL;
    queue.labels = NULL;
    queue.total = 0;
    queue.current_idx = 0;
    pthread_mutex_init(&queue.queue_mutex, NULL);

    indexar_diretorio(dir_treino);
    if (queue.total == 0) {
        fprintf(stderr, "Erro: Nenhum arquivo .emat encontrado em %s\n", dir_treino);
        pthread_mutex_destroy(&queue.queue_mutex);
        return -1;
    }

    embaralhar_fila();

    sem_empty = sem_open(SEM_EMPTY_NAME, O_CREAT | O_EXCL, 0666, g_buffer_capacity);
    sem_full  = sem_open(SEM_FULL_NAME, O_CREAT | O_EXCL, 0666, 0);
    sem_mutex = sem_open(SEM_MUTEX_NAME, O_CREAT | O_EXCL, 0666, 1);

    if (sem_empty == SEM_FAILED || sem_full == SEM_FAILED || sem_mutex == SEM_FAILED) {
        perror("Erro ao criar semáforos POSIX no Produtor");
        limpar_recursos();
        return -1;
    }

    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("Erro ao executar shm_open");
        limpar_recursos();
        return -1;
    }

    size_t total_shm_bytes = shm_buffer_bytes(g_buffer_capacity);
    if (ftruncate(shm_fd, (off_t)total_shm_bytes) != 0) {
        perror("Erro ao executar ftruncate na SHM");
        limpar_recursos();
        return -1;
    }

    shm = mmap(NULL, total_shm_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) {
        perror("Erro no mmap da SHM no Produtor");
        limpar_recursos();
        return -1;
    }

    shm->total_samples = (uint32_t)queue.total;
    shm->capacity = g_buffer_capacity;
    shm->head = 0;
    shm->tail = 0;

    threads_handles = malloc((size_t)g_num_threads * sizeof(pthread_t));
    if (!threads_handles) {
        perror("Erro ao alocar threads do produtor");
        limpar_recursos();
        return -1;
    }

    for (int i = 0; i < g_num_threads; i++) {
        pthread_create(&threads_handles[i], NULL, worker_producer, NULL);
    }

    return queue.total;
}

int iniciar_produtor(const char *dir_treino, int num_threads) {
    return iniciar_produtor_ext(dir_treino, num_threads, DEFAULT_BUFFER_SIZE);
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