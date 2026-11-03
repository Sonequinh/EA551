#ifndef LIBMNIST_H
#define LIBMNIST_H

#include <stdint.h>

#define SHM_NAME        "/shm_mnist"
#define SEM_EMPTY_NAME  "/sem_mnist_empty"
#define SEM_FULL_NAME   "/sem_mnist_full"
#define SEM_MUTEX_NAME  "/sem_mnist_mutex"

#define NUM_SLOTS       2048
#define IMG_BYTES       784

typedef struct __attribute__((packed)) {
    uint32_t linhas;
    uint32_t colunas;
    uint8_t  dtype;
} HeaderEmat;

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

// Funções públicas expostas para a biblioteca compartilhada (.so)
int iniciar_produtor(const char *dir_treino, int num_threads);
void finalizar_produtor(void);
void limpar_recursos(void);

#endif