#ifndef LIBMNIST_H
#define LIBMNIST_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

// IPC POSIX — Modo Teste / Batch
#define SHM_NAME        "/shm_mnist"
#define SEM_EMPTY_NAME  "/sem_mnist_empty"
#define SEM_FULL_NAME   "/sem_mnist_full"
#define SEM_MUTEX_NAME  "/sem_mnist_mutex"

// IPC POSIX — Modo Inferência + Visualização (fenster.h)
#define SHM_INFER_NAME  "/shm_mnist_infer"
#define SEM_INFER_NAME  "/sem_mnist_infer"

#define DEFAULT_BUFFER_SIZE 2048
#define IMG_DIM             128
#define IMG_BYTES           (IMG_DIM * IMG_DIM)

#pragma pack(push, 1)
typedef struct {
    uint32_t linhas;
    uint32_t colunas;
    uint8_t  dtype;
} HeaderEmat;
#pragma pack(pop)

typedef struct {
    uint8_t image[IMG_BYTES];
    int32_t label;
} SampleSlot;

// Buffer circular dinâmico em Memória Compartilhada
typedef struct {
    uint32_t total_samples;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
    SampleSlot slots[];
} SharedBuffer;

static inline size_t shm_buffer_bytes(uint32_t capacity) {
    return sizeof(SharedBuffer) + (size_t)capacity * sizeof(SampleSlot);
}

// Módulo Produtor / DataLoader (produtor.c)
int  iniciar_produtor_ext(const char *dir_treino, int num_threads, int buffer_size);
int  iniciar_produtor(const char *dir_treino, int num_threads);
void finalizar_produtor(void);
void limpar_recursos(void);

// Módulo Visualizador C / X11 (visualizador.c)
pid_t iniciar_visualizador_shm(const double *img_784);
void  aguardar_visualizador_shm(pid_t pid);

// Módulo Álgebra / Inferência / Teste (matmul.c)
double *alocar_matriz_1d(uint32_t linhas, uint32_t colunas);
void    liberar_matriz_1d(double *matriz);
double *ler_emat_1d(const char *caminho, uint32_t *linhas, uint32_t *colunas);
double *multiplicar_matrizes_otimizado(const double *A, uint32_t rA, uint32_t cA,
                                       const double *B, uint32_t rB, uint32_t cB);

int     carregar_pesos_c(const char *caminho_W_emat);
int     classificar_imagem_mnist(const uint8_t *imagem_784);

int     executar_inferencia_ext(const char *caminho_imagem, const char *caminho_pesos, int visualizacao);
int     executar_inferencia_c(const char *caminho_imagem, const char *caminho_pesos);

int     executar_modo_teste_ext(const char *dir_teste, int produtor_threads, int matmul_threads,
                                int consumidores, int buffer_size, const char *caminho_pesos);
int     executar_modo_teste_c(const char *dir_teste, int num_threads, const char *caminho_pesos);

#endif // LIBMNIST_H