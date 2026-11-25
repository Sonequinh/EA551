#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>

// Cabeçalho binário .emat (9 bytes)
#pragma pack(push, 1)
typedef struct {
    uint32_t linhas;
    uint32_t colunas;
    uint8_t  dtype; // 1 = float32
} HeaderEmat;
#pragma pack(pop)

int main(int argc, char *argv[]) {

    // 
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <entrada.npy> <saida.emat>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *input_path = argv[1];
    const char *output_path = argv[2];

    // Abre o arquivo .npy
    int fd_in = open(input_path, O_RDONLY);
    if (fd_in < 0) {
        perror("Erro ao abrir arquivo .npy");
        return EXIT_FAILURE;
    }

    // Validar Magic Bytes (\x93NUMPY)
    char magic[6];
    if (read(fd_in, magic, 6) != 6) {
        perror("Erro ao ler magic bytes do .npy");
        close(fd_in);
        return EXIT_FAILURE;
    }

    if (memcmp(magic, "\x93NUMPY", 6) != 0) {
        fprintf(stderr, "Erro: O arquivo nao esta no formato .npy valido.\n");
        close(fd_in);
        return EXIT_FAILURE;
    }

    // Ler Versão do NumPy: versão principal e secundária
    uint8_t major_ver, minor_ver;
    if (read(fd_in, &major_ver, 1) != 1 || read(fd_in, &minor_ver, 1) != 1) {
        perror("Erro ao ler versao do .npy");
        close(fd_in);
        return EXIT_FAILURE;
    }

    // Ler tamanho do cabeçalho em ASCII
    // Se versão 1.0: header_len armazenado em 2 bytes (uint16_t)
    // Se versão 2.0+: header_Len armazenado em 4 bytes (uint32_t)
    uint32_t header_len = 0;
    if (major_ver == 1) {
        uint16_t h_len_16;
        if (read(fd_in, &h_len_16, 2) != 2) {
            perror("Erro ao ler header_len (v1.0)");
            close(fd_in);
            return EXIT_FAILURE;
        }
        header_len = h_len_16;
    } else {
        if (read(fd_in, &header_len, 4) != 4) {
            perror("Erro ao ler header_len (v2.0+)");
            close(fd_in);
            return EXIT_FAILURE;
        }
    }

    // Ler o texto do dicionário do cabeçalho ASCII do .npy
    char *header_str = malloc(header_len + 1);
    if (!header_str) {
        perror("Erro de alocacao para o cabecalho");
        close(fd_in);
        return EXIT_FAILURE;
    }

    if (read(fd_in, header_str, header_len) != (ssize_t)header_len) {
        perror("Erro ao ler texto do cabecalho .npy");
        free(header_str);
        close(fd_in);
        return EXIT_FAILURE;
    }
    header_str[header_len] = '\0';  // Adiciona o '\0' para sinalizar o fim da string

    // Extrair as dimensões (shape) do cabeçalho ASCII
    uint32_t linhas = 0, colunas = 1;
    char *shape_ptr = strstr(header_str, "'shape':");
    if (!shape_ptr) {
        shape_ptr = strstr(header_str, "\"shape\":");
    }
    if (shape_ptr) {
        char *open_paren = strchr(shape_ptr, '(');
        if (open_paren) {
            // Extrai uma tupla com as dimensões
            if (sscanf(open_paren, "(%u, %u)", &linhas, &colunas) != 2) {
                // Caso tenha apenas uma dimensão atribui colunas = 1
                if (sscanf(open_paren, "(%u,)", &linhas) == 1) {
                    colunas = 1;
                }
            }
        }
    }

    free(header_str);   // Libera o buffer da RAM

    if (linhas == 0) {
        fprintf(stderr, "Erro ao parsear dimensoes (shape) do .npy\n");
        close(fd_in);
        return EXIT_FAILURE;
    }

    printf("Dimensoes detectadas: %u linhas x %u colunas\n", linhas, colunas);

    // Ler os dados binários dos pesos
    size_t total_elements = (size_t)linhas * colunas;
    size_t total_bytes = total_elements * sizeof(float);

    float *data_buf = malloc(total_bytes);
    if (!data_buf) {
        perror("Erro ao alocar memoria para a matriz");
        close(fd_in);
        return EXIT_FAILURE;
    }

    // Garante toda leitura do bloco binário de dados
    size_t bytes_lidos = 0;
    while (bytes_lidos < total_bytes) {
        ssize_t res = read(fd_in, (char *)data_buf + bytes_lidos, total_bytes - bytes_lidos);
        if (res <= 0) {
            if (res < 0) perror("Erro durante leitura do payload .npy");
            break;
        }
        bytes_lidos += res;
    }
    close(fd_in);   // Concluida a leitura do arquivo .npy fecha ele

    // Criação(O_CREAT) ou sobrescrita(O_TRUNC) Do arquivo .emat
    // O 0644 define as permissões do arquivo
    // 0644: owner|group|others -> rw-|r--|r--
    int fd_out = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_out < 0) {
        perror("Erro ao criar o arquivo .emat");
        free(data_buf);
        return EXIT_FAILURE;
    }

    // Cabeçalho binário
    HeaderEmat header_emat;
    header_emat.linhas = linhas;
    header_emat.colunas = colunas;
    header_emat.dtype = 1; // 1 representa float32

    if (write(fd_out, &header_emat, sizeof(HeaderEmat)) != sizeof(HeaderEmat)) {
        perror("Erro ao escrever cabecalho .emat");
        close(fd_out);
        free(data_buf);
        return EXIT_FAILURE;
    }

    // Escreve os dados bin da matriz no arquivo .emat
    size_t bytes_escritos = 0;
    while (bytes_escritos < total_bytes) {
        ssize_t res = write(fd_out, (char *)data_buf + bytes_escritos, total_bytes - bytes_escritos);
        if (res <= 0) {
            perror("Erro durante escrita do payload .emat");
            break;
        }
        bytes_escritos += res;
    }

    // Encerra o arquivo e libera o buffer 
    close(fd_out);
    free(data_buf);

    printf("Conversao concluida com sucesso -> '%s'\n", output_path);
    return EXIT_SUCCESS;
}