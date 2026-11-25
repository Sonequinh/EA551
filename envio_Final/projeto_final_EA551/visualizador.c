#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <semaphore.h>
#include "libmnist.h"

#define FENSTER_API static inline
#include "fenster.h"

#define FENSTER_SCALE  4


static volatile sig_atomic_t g_stop_vis = 0;
static struct sigaction g_old_sigint;
static int g_sigint_installed = 0;

static void tratar_sigint_vis(int sig) {
    (void)sig;
    g_stop_vis = 1;
}

static void limpar_ipc_visualizador(uint8_t *shm_ptr, int shm_fd, sem_t *sem, int unlink_ipc) {
    if (shm_ptr && shm_ptr != MAP_FAILED) munmap(shm_ptr, IMG_BYTES);
    if (shm_fd >= 0) close(shm_fd);
    if (sem && sem != SEM_FAILED) sem_close(sem);
    if (unlink_ipc) {
        shm_unlink(SHM_INFER_NAME);
        sem_unlink(SEM_INFER_NAME);
    }
}

static void loop_janela_filho(uint8_t *shm_pixels, int shm_fd, sem_t *sem) {
    // Aguarda o processo pai gravar os pixels na SHM
    while (sem_wait(sem) != 0) {
        if (errno == EINTR && g_stop_vis) {
            limpar_ipc_visualizador(shm_pixels, shm_fd, sem, 0);
            _exit(0);
        }
    }

    if (g_stop_vis) {
        limpar_ipc_visualizador(shm_pixels, shm_fd, sem, 0);
        _exit(0);
    }

    const int win_w = IMG_DIM * FENSTER_SCALE;
    const int win_h = IMG_DIM * FENSTER_SCALE;
    uint32_t buf[IMG_DIM * FENSTER_SCALE * IMG_DIM * FENSTER_SCALE];

    struct fenster f = {
        .title  = "EA551 MNIST - Visualizador C (fenster.h)",
        .width  = win_w,
        .height = win_h,
        .buf    = buf
    };

    if (fenster_open(&f) != 0) {
        limpar_ipc_visualizador(shm_pixels, shm_fd, sem, 0);
        _exit(1);
    }

    // Escala a imagem 28x28 (0..255) para 280x280 em tons de cinza
    for (int y = 0; y < IMG_DIM; y++) {
        for (int x = 0; x < IMG_DIM; x++) {
            uint8_t p = shm_pixels[y * IMG_DIM + x];
            uint32_t cor = ((uint32_t)p << 16) | ((uint32_t)p << 8) | (uint32_t)p;
            for (int dy = 0; dy < FENSTER_SCALE; dy++) {
                for (int dx = 0; dx < FENSTER_SCALE; dx++) {
                    fenster_pixel(&f, x * FENSTER_SCALE + dx, y * FENSTER_SCALE + dy) = cor;
                }
            }
        }
    }

    // Mantém a janela aberta até ESC (27), fechamento X11 ou SIGINT
    while (!g_stop_vis && fenster_loop(&f) == 0 && !f.keys[27]) {
        fenster_sleep(16);
    }

    fenster_close(&f);
    limpar_ipc_visualizador(shm_pixels, shm_fd, sem, 0);
    _exit(0);
}

pid_t iniciar_visualizador_shm(const double *img_784) {
    g_stop_vis = 0;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = tratar_sigint_vis;
    if (sigaction(SIGINT, &sa, &g_old_sigint) == 0) {
        g_sigint_installed = 1;
    }

    shm_unlink(SHM_INFER_NAME);
    sem_unlink(SEM_INFER_NAME);

    sem_t *sem = sem_open(SEM_INFER_NAME, O_CREAT | O_EXCL, 0666, 0);
    int shm_fd = shm_open(SHM_INFER_NAME, O_CREAT | O_RDWR, 0666);
    if (sem == SEM_FAILED || shm_fd < 0 || ftruncate(shm_fd, IMG_BYTES) != 0) {
        perror("Erro ao inicializar IPC do visualizador");
        limpar_ipc_visualizador(NULL, shm_fd, sem, 1);
        return -1;
    }

    uint8_t *shm_pixels = mmap(NULL, IMG_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_pixels == MAP_FAILED) {
        perror("Erro no mmap do visualizador");
        limpar_ipc_visualizador(NULL, shm_fd, sem, 1);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("Erro ao executar fork()");
        limpar_ipc_visualizador(shm_pixels, shm_fd, sem, 1);
        return -1;
    }
    if (pid == 0) {
        loop_janela_filho(shm_pixels, shm_fd, sem);
    }

    // Processo pai grava a matriz na SHM e libera o semáforo para o filho
    for (int i = 0; i < IMG_BYTES; i++) {
        double v = img_784[i];
        shm_pixels[i] = (uint8_t)(v < 0.0 ? 0.0 : (v > 255.0 ? 255.0 : v));
    }
    sem_post(sem);

    limpar_ipc_visualizador(shm_pixels, shm_fd, sem, 0);
    return pid;
}

void aguardar_visualizador_shm(pid_t pid) {
    if (pid > 0) {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0) {
            if (errno == EINTR) {
                if (g_stop_vis) kill(pid, SIGTERM);
                continue;
            }
            break;
        }
    }

    shm_unlink(SHM_INFER_NAME);
    sem_unlink(SEM_INFER_NAME);

    if (g_sigint_installed) {
        sigaction(SIGINT, &g_old_sigint, NULL);
        g_sigint_installed = 0;
    }
}