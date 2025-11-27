#include "lwip/sockets.h"
#include "string.h"
#include "stdio.h"
#include "cmsis_os.h"

#define SERVER_IP   "192.168.2.40"
#define SERVER_PORT 5000

void tcp_client(void *argument)
{
    int sock;
    struct sockaddr_in server_addr;
    char tx_buffer[100];
    char rx_buffer[256];

    LWIP_UNUSED_ARG(argument);

    // Wait a moment to make sure server is up
    osDelay(2000);

    // Create socket
    sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Socket creation failed!\r\n");
        vTaskDelete(NULL);
    }

    // Setup server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Connect to server
    if (lwip_connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Connect failed!\r\n");
        lwip_close(sock);
        vTaskDelete(NULL);
    }

    printf("Connected to server!\r\n");

    while (1) {
        // Send command
        snprintf(tx_buffer, sizeof(tx_buffer), "TEMP?\r\n");
        if (lwip_send(sock, tx_buffer, strlen(tx_buffer), 0) < 0) {
            printf("Send failed!\r\n");
            break;
        }

        // Receive reply
        int len = lwip_recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len > 0) {
            rx_buffer[len] = '\0';
            printf("Server replied: %s", rx_buffer);
        } else {
            printf("Server disconnected or error!\r\n");
            break;
        }

        osDelay(2000); // Wait 2 seconds
    }

    lwip_close(sock);
    vTaskDelete(NULL);
}
