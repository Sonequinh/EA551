#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <time.h>
#include <math.h>
#include <omp.h>
#include "libmnist.h"

// Carregamento de pesos para o teste de classificação MNIST
static double *pesos_W = NULL; // Matriz W (10 x 784)
static uint32_t W_linhas = 0, W_colunas = 0;

// Aloca matriz em um único bloco continuo
double *alocar_matriz_1d(uint32_t linhas, uint32_t colunas) {
    double *matriz = (double *)malloc((size_t)linhas * colunas * sizeof(double));
    if (!matriz) {
        perror("Erro ao alocar matriz");
        return NULL;
    }
    return matriz;
}

void liberar_matriz_1d(double *matriz) {
    if (matriz) free(matriz);
}

// Lê o arquivo .emat diretamente para um buffer 1D
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
    size_t total_elementos = (size_t)header.linhas * header.colunas;

    double *matriz = alocar_matriz_1d(header.linhas, header.colunas);
    if (!matriz) {
        close(fd);
        return NULL;
    }

    // Leitura por tipo de dado (uint8, float, double)
    if (header.dtype == 0) { // uint8_t
        uint8_t *buf = malloc(total_elementos * sizeof(uint8_t));
        if (read(fd, buf, total_elementos) == (ssize_t)total_elementos) {
            for (size_t i = 0; i < total_elementos; i++) matriz[i] = (double)buf[i];
        }
        free(buf);
    } else if (header.dtype == 1) { // float
        float *buf = malloc(total_elementos * sizeof(float));
        if (read(fd, buf, total_elementos * sizeof(float)) == (ssize_t)(total_elementos * sizeof(float))) {
            for (size_t i = 0; i < total_elementos; i++) matriz[i] = (double)buf[i];
        }
        free(buf);
    } else if (header.dtype == 2) { // double
        if (read(fd, matriz, total_elementos * sizeof(double)) != (ssize_t)(total_elementos * sizeof(double))) {
            fprintf(stderr, "Erro ao ler dados double de %s\n", caminho);
        }
    } else {
        fprintf(stderr, "dtype %u desconhecido\n", header.dtype);
        liberar_matriz_1d(matriz);
        close(fd);
        return NULL;
    }

    close(fd);
    return matriz;
}

// Multiplicação de Matrizes Otimizada 
double *multiplicar_matrizes_otimizado(const double *A, uint32_t rA, uint32_t cA, const double *B, uint32_t rB, uint32_t cB) {
    if (cA != rB) {
        fprintf(stderr, "Erro de dimensões: (%ux%u) x (%ux%u)\n", rA, cA, rB, cB);
        return NULL;
    }

    double *C = alocar_matriz_1d(rA, cB);
    if (!C) return NULL;

    // Inicializa a matriz C com zeros
    memset(C, 0, (size_t)rA * cB * sizeof(double));

    // Reordenação IKJ paralela
    #pragma omp parallel for collapse(1) schedule(static)
    for (uint32_t i = 0; i < rA; i++) {
        for (uint32_t k = 0; k < cA; k++) {
            double temp = A[i * cA + k];
            uint32_t idx_B = k * cB;
            uint32_t idx_C = i * cB;
            
            #pragma omp simd
            for (uint32_t j = 0; j < cB; j++) {
                C[idx_C + j] += temp * B[idx_B + j];
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

    if (W_linhas != 10 || W_colunas != 784) {
        fprintf(stderr, "Erro: Dimensão dos pesos incorreta: (%ux%u), esperado (10x784)\n", 
                W_linhas, W_colunas);
        return -1;
    }
    
    return 0; // Pesos carregados com sucesso
}

// Classifica uma única imagem (784 pixels uint8) retornando o dígito predito (0-9)
int classificar_imagem_mnist(const uint8_t *imagem_784) {
    double max_score = -1e9;
    int classe_predita = -1;

    // Predição: scores = W (10 x 784) * img (784 x 1)
    for (int i = 0; i < 10; i++) {
        double score = 0.0;
        int offset = i * 784;
        
        for (int k = 0; k < 784; k++) {
            score += pesos_W[offset + k] * (double)imagem_784[k];
        }

        if (score > max_score) {
            max_score = score;
            classe_predita = i;
        }
    }

    return classe_predita;
}

int executar_inferencia_c(const char *caminho_imagem, const char *caminho_pesos) {
    // 1. Carrega a imagem .emat e a matriz de pesos W
    uint32_t r, c;
    double *img = ler_emat_1d(caminho_imagem, &r, &c);
    if (!img) return -1;

    if (carregar_pesos_c(caminho_pesos) != 0) {
        liberar_matriz_1d(img);
        return -1;
    }

    // 2. Criação do processo filho via fork()
    pid_t pid = fork();

    if (pid < 0) {
        perror("Erro ao executar fork()");
        return -1;
    } 
    else if (pid == 0) {
        // --- PROCESSO FILHO: Visualização da Imagem ---
        // Pode chamar um script/utilitário externo para abrir a janela
        char *args[] = {"python3", "visualizar.py", (char *)caminho_imagem, NULL};
        execvp("python3", args);
        
        // Se execvp falhar:
        perror("Erro ao abrir janela de visualização");
        exit(1);
    } 
    else {
        // --- PROCESSO PAI: Computação da Classificação ---
        double scores[10] = {0};
        int melhor_classe = 0;
        double max_score = -1e9;

        printf("\n================ MODO INFERÊNCIA ================\n");
        printf("Scores de confiança por dígito:\n");

        for (int i = 0; i < 10; i++) {
            for (int k = 0; k < 784; k++) {
                scores[i] += pesos_W[i * 784 + k] * img[k];
            }
            printf("  Dígito [%d]: %10.4f\n", i, scores[i]);

            if (scores[i] > max_score) {
                max_score = scores[i];
                melhor_classe = i;
            }
        }

        printf("-------------------------------------------------\n");
        printf("PREDIÇÃO DO MODELO: %d (Score: %.4f)\n", melhor_classe, max_score);
        printf("=================================================\n");

        // Aguarda o filho fechar a janela antes de terminar
        int status;
        waitpid(pid, &status, 0);
        liberar_matriz_1d(img);
    }

    return 0;
}



int executar_modo_teste_c(const char *dir_teste, int num_threads, const char *caminho_pesos) {
    // 1. Carrega a matriz de pesos W (10 x 784) se ainda não estiver carregada
    if (carregar_pesos_c(caminho_pesos) != 0) {
        fprintf(stderr, "Erro ao carregar pesos W do arquivo %s\n", caminho_pesos);
        return -1;
    }

    // 2. Inicia o Produtor (DataLoader) em C que popula a SHM
    int total_amostras = iniciar_produtor(dir_teste, num_threads);
    if (total_amostras <= 0) {
        fprintf(stderr, "Erro ao iniciar o produtor para o diretório: %s\n", dir_teste);
        return -1;
    }

    // 3. Conecta aos semáforos POSIX criados pelo Produtor
    sem_t *sem_empty = sem_open(SEM_EMPTY_NAME, 0);
    sem_t *sem_full  = sem_open(SEM_FULL_NAME, 0);
    sem_t *sem_mutex = sem_open(SEM_MUTEX_NAME, 0);

    if (sem_empty == SEM_FAILED || sem_full == SEM_FAILED || sem_mutex == SEM_FAILED) {
        perror("Erro ao abrir semáforos POSIX no Consumidor");
        finalizar_produtor();
        return -1;
    }

    // 4. Conecta à Memória Compartilhada (SHM)
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("Erro ao abrir SHM no Consumidor");
        finalizar_produtor();
        return -1;
    }

    SharedBuffer *shm = mmap(NULL, sizeof(SharedBuffer), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) {
        perror("Erro no mmap da SHM no Consumidor");
        close(shm_fd);
        finalizar_produtor();
        return -1;
    }

    printf("\n==================== MODO TESTE (BATCH) ====================\n");
    printf("Carregando e classificando %d imagens da pasta '%s'...\n", total_amostras, dir_teste);

    // Marca tempo inicial de alta precisão
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int acertos = 0;

    // 5. Laço Principal do Consumidor: consome todas as imagens da SHM
    for (int i = 0; i < total_amostras; i++) {
        // Sincronização POSIX: aguarda slot cheio
        sem_wait(sem_full);
        sem_wait(sem_mutex);

        // Retira a amostra da SHM
        SampleSlot slot = shm->slots[shm->tail];
        shm->tail = (shm->tail + 1) % NUM_SLOTS;

        // Libera seção crítica e avisa que há slot livre
        sem_post(sem_mutex);
        sem_post(sem_empty);

        // Inferência C via matmul: W (10x784) * img (784x1)
        int predicao = classificar_imagem_mnist(slot.image);

        if (predicao == slot.label) {
            acertos++;
        }
    }

    // Marca tempo final
    clock_gettime(CLOCK_MONOTONIC, &end);
    double tempo_total = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double acuracia = ((double)acertos / total_amostras) * 100.0;
    double fps = total_amostras / tempo_total;

    // Relatório final de execução
    printf("-------------------------------------------------------------\n");
    printf("Total de Amostras Processadas : %d\n", total_amostras);
    printf("Predições Corretas           : %d\n", acertos);
    printf("Acurácia Global              : %.2f%%\n", acuracia);
    printf("Tempo Total de Processamento : %.4f segundos\n", tempo_total);
    printf("Vazão (Throughput)           : %.2f imagens/segundo\n", fps);
    printf("=============================================================\n\n");

    // Encerramento e limpeza dos recursos POSIX
    munmap(shm, sizeof(SharedBuffer));
    close(shm_fd);
    sem_close(sem_empty);
    sem_close(sem_full);
    sem_close(sem_mutex);

    finalizar_produtor();
    return 0;
}