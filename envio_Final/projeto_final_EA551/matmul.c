#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <pthread.h>
#include <time.h>
#include "libmnist.h"

#define TILE_K 64

static double *pesos_W = NULL; // Matriz W (10 x 16384) em bloco 1D contínuo (somente leitura durante o teste)
static uint32_t W_linhas = 0, W_colunas = 0;

double *alocar_matriz_1d(uint32_t linhas, uint32_t colunas) {
    double *matriz = (double *)malloc((size_t)linhas * colunas * sizeof(double));
    if (!matriz) perror("Erro ao alocar matriz");
    return matriz;
}

void liberar_matriz_1d(double *matriz) {
    if (matriz) free(matriz);
}

// Lê o arquivo .emat diretamente via syscall read() para um buffer 1D
double *ler_emat_1d(const char *caminho, uint32_t *linhas, uint32_t *colunas) {
    int fd = open(caminho, O_RDONLY);
    if (fd < 0) {
        perror("Erro ao abrir arquivo .emat");
        return NULL;
    }

    HeaderEmat header;
    if (read(fd, &header, sizeof(HeaderEmat)) != sizeof(HeaderEmat)) {
        fprintf(stderr, "Erro ao ler cabeçalho de %s\n", caminho);
        close(fd);
        return NULL;
    }

    *linhas = header.linhas;
    *colunas = header.colunas;
    size_t total = (size_t)header.linhas * header.colunas;

    double *matriz = alocar_matriz_1d(header.linhas, header.colunas);
    if (!matriz) {
        close(fd);
        return NULL;
    }

    if (header.dtype == 0) { // uint8_t
        uint8_t *buf = malloc(total * sizeof(uint8_t));
        if (!buf || read(fd, buf, total) != (ssize_t)total) {
            free(buf); liberar_matriz_1d(matriz); close(fd); return NULL;
        }
        for (size_t i = 0; i < total; i++) matriz[i] = (double)buf[i];
        free(buf);
    } else if (header.dtype == 1) { // float
        float *buf = malloc(total * sizeof(float));
        if (!buf || read(fd, buf, total * sizeof(float)) != (ssize_t)(total * sizeof(float))) {
            free(buf); liberar_matriz_1d(matriz); close(fd); return NULL;
        }
        for (size_t i = 0; i < total; i++) matriz[i] = (double)buf[i];
        free(buf);
    } else if (header.dtype == 2) { // double
        if (read(fd, matriz, total * sizeof(double)) != (ssize_t)(total * sizeof(double))) {
            liberar_matriz_1d(matriz); close(fd); return NULL;
        }
    } else {
        fprintf(stderr, "dtype %u desconhecido\n", header.dtype);
        liberar_matriz_1d(matriz); close(fd); return NULL;
    }

    close(fd);
    return matriz;
}

// Multiplicação de Matrizes Otimizada em C puro (sem OpenMP) sobre arrays 1D:
// Reordenação de laços I-K-J + Cache Blocking (Tiling)
double *multiplicar_matrizes_otimizado(const double *A, uint32_t rA, uint32_t cA,
                                       const double *B, uint32_t rB, uint32_t cB) {
    if (cA != rB) {
        fprintf(stderr, "Erro de dimensões: (%ux%u) x (%ux%u)\n", rA, cA, rB, cB);
        return NULL;
    }

    double *C = alocar_matriz_1d(rA, cB);
    if (!C) return NULL;
    memset(C, 0, (size_t)rA * cB * sizeof(double));

    for (uint32_t kk = 0; kk < cA; kk += TILE_K) {
        uint32_t k_max = (kk + TILE_K < cA) ? (kk + TILE_K) : cA;
        for (uint32_t i = 0; i < rA; i++) {
            double *row_C = C + (size_t)i * cB;
            const double *row_A = A + (size_t)i * cA;
            for (uint32_t k = kk; k < k_max; k++) {
                double a_ik = row_A[k];
                const double *row_B = B + (size_t)k * cB;
                for (uint32_t j = 0; j < cB; j++) {
                    row_C[j] += a_ik * row_B[j];
                }
            }
        }
    }

    return C;
}

int carregar_pesos_c(const char *caminho_W_emat) {
    if (pesos_W) {
        liberar_matriz_1d(pesos_W);
        pesos_W = NULL;
    }

    pesos_W = ler_emat_1d(caminho_W_emat, &W_linhas, &W_colunas);
    if (!pesos_W) return -1;

    if (W_linhas != 10 || W_colunas != IMG_BYTES) {
        fprintf(stderr, "Erro: Dimensão dos pesos incorreta: (%ux%u), esperado (10x%d)\n",
                W_linhas, W_colunas, IMG_BYTES);
        liberar_matriz_1d(pesos_W);
        pesos_W = NULL;
        return -1;
    }
    return 0;
}

// Classifica uma única imagem (16384 pixels uint8) usando multiplicar_matrizes_otimizado
int classificar_imagem_mnist(const uint8_t *imagem_784) {
    if (!pesos_W || !imagem_784) return -1;

    double x[IMG_BYTES];
    for (int k = 0; k < IMG_BYTES; k++) x[k] = (double)imagem_784[k];

    double *y = multiplicar_matrizes_otimizado(pesos_W, 10, IMG_BYTES, x, IMG_BYTES, 1);
    if (!y) return -1;

    int predicao = 0;
    double max_score = y[0];
    for (int i = 1; i < 10; i++) {
        if (y[i] > max_score) {
            max_score = y[i];
            predicao = i;
        }
    }

    liberar_matriz_1d(y);
    return predicao;
}

int executar_inferencia_ext(const char *caminho_imagem, const char *caminho_pesos, int visualizacao) {
    uint32_t r = 0, c = 0;
    double *img = ler_emat_1d(caminho_imagem, &r, &c);
    if (!img) return -1;

    if ((size_t)r * c != IMG_BYTES || carregar_pesos_c(caminho_pesos) != 0) {
        liberar_matriz_1d(img);
        return -1;
    }

    // Inicia visualizador C (fenster.h + SHM + fork) apenas se visualizacao == 1
    pid_t pid_vis = -1;
    if (visualizacao) {
        pid_vis = iniciar_visualizador_shm(img);
        if (pid_vis < 0) {
            liberar_matriz_1d(img);
            return -1;
        }
    }

    // Predição matricial: y (10 x 1) = W (10 x 16384) * img (16384 x 1)
    double *scores = multiplicar_matrizes_otimizado(pesos_W, 10, IMG_BYTES, img, IMG_BYTES, 1);
    if (!scores) {
        if (visualizacao) aguardar_visualizador_shm(pid_vis);
        liberar_matriz_1d(img);
        return -1;
    }

    int melhor_classe = 0;
    double max_score = scores[0];
    printf("\n================ MODO INFERÊNCIA ================\n");
    printf("Scores de confiança por dígito:\n");
    for (int i = 0; i < 10; i++) {
        printf("  Dígito [%d]: %10.4f\n", i, scores[i]);
        if (scores[i] > max_score) {
            max_score = scores[i];
            melhor_classe = i;
        }
    }
    printf("-------------------------------------------------\n");
    printf("PREDIÇÃO DO MODELO: %d (Score: %.4f)\n", melhor_classe, max_score);
    printf("=================================================\n");

    liberar_matriz_1d(scores);
    if (visualizacao) aguardar_visualizador_shm(pid_vis);
    liberar_matriz_1d(img);
    return 0;
}

int executar_inferencia_c(const char *caminho_imagem, const char *caminho_pesos) {
    return executar_inferencia_ext(caminho_imagem, caminho_pesos, 1);
}

// Contexto compartilhado das threads consumidoras (pthread)
typedef struct {
    SharedBuffer *shm;
    sem_t *sem_empty;
    sem_t *sem_full;
    sem_t *sem_mutex;
    int total_amostras;

    pthread_mutex_t lock_fila;   // Sincroniza a reserva de amostras (evita deadlock no sem_wait)
    int consumidas;

    pthread_mutex_t lock_stats;  // Sincroniza a integração global de acertos e erros
    int acertos;
    int erros;
} ConsumerContext;

// Cada thread consumidora retira amostras da SHM, classifica em paralelo e acumula localmente
static void *worker_consumidor(void *arg) {
    ConsumerContext *ctx = (ConsumerContext *)arg;
    int acertos_locais = 0;
    int erros_locais = 0;

    while (1) {
        // 1. Reserva um "ticket" antes de aguardar no semáforo
        pthread_mutex_lock(&ctx->lock_fila);
        if (ctx->consumidas >= ctx->total_amostras) {
            pthread_mutex_unlock(&ctx->lock_fila);
            break;
        }
        ctx->consumidas++;
        pthread_mutex_unlock(&ctx->lock_fila);

        // 2. Consome 1 slot da memória compartilhada (seção crítica do buffer circular)
        sem_wait(ctx->sem_full);
        sem_wait(ctx->sem_mutex);

        SampleSlot slot = ctx->shm->slots[ctx->shm->tail];
        ctx->shm->tail = (ctx->shm->tail + 1) % ctx->shm->capacity;

        sem_post(ctx->sem_mutex);
        sem_post(ctx->sem_empty);

        // 3. Classificação em paralelo (pesos_W é somente leitura)
        if (slot.label >= 0) {
            int pred = classificar_imagem_mnist(slot.image);
            if (pred == slot.label) {
                acertos_locais++;
            } else {
                erros_locais++;
            }
        }
    }

    // 4. Integra os contadores locais aos globais sob proteção de mutex (reduz contenção)
    pthread_mutex_lock(&ctx->lock_stats);
    ctx->acertos += acertos_locais;
    ctx->erros += erros_locais;
    pthread_mutex_unlock(&ctx->lock_stats);

    return NULL;
}

int executar_modo_teste_ext(const char *dir_teste, int produtor_threads, int matmul_threads,
                            int consumidores, int buffer_size, const char *caminho_pesos) {
    if (produtor_threads < 1) produtor_threads = 1;
    if (matmul_threads < 1) matmul_threads = 1;
    if (consumidores < 1) consumidores = 1;
    if (buffer_size < 1) buffer_size = DEFAULT_BUFFER_SIZE;

    if (carregar_pesos_c(caminho_pesos) != 0) return -1;

    int total_amostras = iniciar_produtor_ext(dir_teste, produtor_threads, buffer_size);
    if (total_amostras <= 0) return -1;

    sem_t *sem_empty = sem_open(SEM_EMPTY_NAME, 0);
    sem_t *sem_full  = sem_open(SEM_FULL_NAME, 0);
    sem_t *sem_mutex = sem_open(SEM_MUTEX_NAME, 0);

    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    size_t shm_bytes = shm_buffer_bytes((uint32_t)buffer_size);
    SharedBuffer *shm = (shm_fd >= 0) ? mmap(NULL, shm_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0) : MAP_FAILED;

    if (sem_empty == SEM_FAILED || sem_full == SEM_FAILED || sem_mutex == SEM_FAILED || shm == MAP_FAILED) {
        perror("Erro ao conectar ao IPC no Consumidor");
        if (shm != MAP_FAILED) munmap(shm, shm_bytes);
        if (shm_fd >= 0) close(shm_fd);
        if (sem_empty != SEM_FAILED) sem_close(sem_empty);
        if (sem_full  != SEM_FAILED) sem_close(sem_full);
        if (sem_mutex != SEM_FAILED) sem_close(sem_mutex);
        finalizar_produtor();
        return -1;
    }

    printf("\n==================== MODO TESTE (BATCH) ====================\n");
    printf("Classificando %d imagens de '%s' (produtor=%d, consumidores=%d, matmul=%d, buffer=%u)...\n",
           total_amostras, dir_teste, produtor_threads, consumidores, matmul_threads, shm->capacity);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    ConsumerContext ctx = {
        .shm = shm,
        .sem_empty = sem_empty,
        .sem_full = sem_full,
        .sem_mutex = sem_mutex,
        .total_amostras = total_amostras,
        .consumidas = 0,
        .acertos = 0,
        .erros = 0
    };
    pthread_mutex_init(&ctx.lock_fila, NULL);
    pthread_mutex_init(&ctx.lock_stats, NULL);

    pthread_t *threads = malloc((size_t)consumidores * sizeof(pthread_t));
    if (!threads) {
        perror("Erro ao alocar threads consumidoras");
        pthread_mutex_destroy(&ctx.lock_fila);
        pthread_mutex_destroy(&ctx.lock_stats);
        munmap(shm, shm_bytes);
        close(shm_fd);
        sem_close(sem_empty);
        sem_close(sem_full);
        sem_close(sem_mutex);
        finalizar_produtor();
        return -1;
    }

    for (int t = 0; t < consumidores; t++) {
        pthread_create(&threads[t], NULL, worker_consumidor, &ctx);
    }
    for (int t = 0; t < consumidores; t++) {
        pthread_join(threads[t], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double tempo_total = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    int avaliadas = ctx.acertos + ctx.erros;
    double acuracia = (avaliadas > 0) ? ((double)ctx.acertos / avaliadas) * 100.0 : 0.0;
    double fps = total_amostras / tempo_total;

    printf("-------------------------------------------------------------\n");
    printf("Total de Amostras Processadas : %d (Acertos + Erros = %d)\n", total_amostras, avaliadas);
    printf("Predições Corretas (Acertos)  : %d\n", ctx.acertos);
    printf("Predições Incorretas (Erros)  : %d\n", ctx.erros);
    printf("Acurácia Global               : %.2f%%\n", acuracia);
    printf("Tempo Total de Processamento  : %.4f segundos\n", tempo_total);
    printf("Vazão (Throughput)            : %.2f imagens/segundo\n", fps);
    printf("=============================================================\n\n");

    free(threads);
    pthread_mutex_destroy(&ctx.lock_fila);
    pthread_mutex_destroy(&ctx.lock_stats);
    munmap(shm, shm_bytes);
    close(shm_fd);
    sem_close(sem_empty);
    sem_close(sem_full);
    sem_close(sem_mutex);

    finalizar_produtor();
    return 0;
}

int executar_modo_teste_c(const char *dir_teste, int num_threads, const char *caminho_pesos) {
    return executar_modo_teste_ext(dir_teste, num_threads, 1, num_threads, DEFAULT_BUFFER_SIZE, caminho_pesos);
}