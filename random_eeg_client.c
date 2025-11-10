#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

// --- CONFIGURACIÓN CON #define (CORREGIDO) ---
#define SERVER_PORT 9090      // Puerto donde escucha el Hub de Python
#define SERVER_IP "127.0.0.1" // localhost

#define NUM_CHANNELS 8
#define SAMPLES_PER_CHANNEL 1000
#define TOTAL_POINTS (NUM_CHANNELS * SAMPLES_PER_CHANNEL) 
#define LOOP_DELAY_US 50000 

// Buffer de datos global (ahora válido)
char data_buffer[TOTAL_POINTS * 15];


int connect_to_server() {
    int sock_fd;
    struct sockaddr_in serv_addr;

    printf("[C Client] Intentando conectar a %s:%d...\n", SERVER_IP, SERVER_PORT);

    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error al crear socket");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);

    if(inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("Dirección IP inválida");
        return -1;
    }

    // Bucle de reconexión
    while (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error de conexión");
        printf("[C Client] Reintentando en 2 segundos...\n");
        sleep(2);
    }

    printf("[C Client] ¡Conectado al Hub de Python!\n");
    return sock_fd;
}


int main() {
    int sock_fd = -1;
    static double global_time = 0;

    srand(time(NULL));

    while (1) {
        if (sock_fd < 0) {
            sock_fd = connect_to_server();
            if (sock_fd < 0) {
                printf("[C Client] Falló la conexión, saliendo.\n");
                return 1;
            }
        }

        int offset = 0;
        for (int s = 0; s < SAMPLES_PER_CHANNEL; s++) {
            for (int c = 0; c < NUM_CHANNELS; c++) {
                double base_signal = sin(global_time * (c + 1) * 0.1 + s / 20.0);
                double noise = ((double)rand() / RAND_MAX - 0.5) * 0.2;
                double final_val = base_signal + noise;

                offset += sprintf(data_buffer + offset, "%.4f,", final_val);
            }
        }
        
        data_buffer[offset - 1] = '\n'; // Reemplaza la última coma
        data_buffer[offset] = '\0';

        if (send(sock_fd, data_buffer, offset, 0) < 0) {
            perror("Error al enviar (send)");
            printf("[C Client] Hub desconectado. Intentando reconectar...\n");
            close(sock_fd);
            sock_fd = -1;
        }
        
        global_time += 0.1;
        usleep(LOOP_DELAY_US);
    }

    close(sock_fd);
    return 0;
}
```eof

