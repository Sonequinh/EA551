#ifndef LIBMNIST_H
#define LIBMNIST_H

#include <stdint.h>
#include <stddef.h>

// Definições da SHM e Semáforos POSIX
#define SHM_NAME        "/shm_mnist"
#define SEM_EMPTY_NAME  "/sem_mnist_empty"
#define SEM_FULL_NAME   "/sem_mnist_full"
#define SEM_MUTEX_NAME  "/sem_mnist_mutex"

#define NUM_SLOTS       2048
#define IMG_BYTES       784

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

typedef struct {
    uint32_t total_samples;
    uint32_t head;
    uint32_t tail;
    SampleSlot slots[NUM_SLOTS];
} SharedBuffer;

// Módulo DataLoader / Produtor (produtor.c)
int  iniciar_produtor(const char *dir_treino, int num_threads);
void finalizar_produtor(void);
void limpar_recursos(void);

// Módulo de Álgebra / Inferência / Teste (matmul.c)
double *ler_emat_1d(const char *caminho, uint32_t *linhas, uint32_t *colunas);
void    liberar_matriz_1d(double *matriz);
int     carregar_pesos_c(const char *caminho_W_emat);
int     classificar_imagem_mnist(const uint8_t *imagem_784);
int     executar_inferencia_c(const char *caminho_imagem, const char *caminho_pesos);
int     executar_modo_teste_c(const char *dir_teste, int num_threads, const char *caminho_pesos);

#endif // LIBMNIST_H