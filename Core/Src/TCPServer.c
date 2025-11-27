#include "lwip/tcp.h"
#include "lwip/tcpip.h"
#include "string.h"
#include "stdio.h"
#include "main.h"
#include "cmsis_os.h"

#define SERVER_PORT 5000
#define MAX_CMD_LEN 256

static struct tcp_pcb *server_pcb;

// --- FreeRTOS queue for TCP commands ---
typedef struct {
    struct tcp_pcb *pcb;
    char cmd[MAX_CMD_LEN];
} tcp_msg_t;

typedef struct {
    struct tcp_pcb *pcb;
    char reply[MAX_CMD_LEN];
} tcp_reply_t;

osMessageQueueId_t tcpQueueHandle;

// --- Simulated temperature ---
float get_temperature(void) {
    return 23.5f;
}

// --- Command handler ---
void handle_command(const char *cmd, char *response, size_t len)
{
    float temp = get_temperature();
    char status[32];

    if (temp <= 24.0f) strcpy(status, "GREEN (SAFE)");
    else if (temp <= 27.0f) strcpy(status, "YELLOW (CAUTION)");
    else strcpy(status, "RED (ALARM)");

    if (strcasecmp(cmd, "STATUS") == 0 || strcasecmp(cmd, "TEMP") == 0)
        snprintf(response, len, "Temperature: %.2f C | Status: %s\r\n", temp, status);
    else if (strcasecmp(cmd, "LED ON") == 0) {
        HAL_GPIO_WritePin(GPIOB, LED_Pin, GPIO_PIN_SET);
        snprintf(response, len, "LED is ON\r\n");
    } else if (strcasecmp(cmd, "LED OFF") == 0) {
        HAL_GPIO_WritePin(GPIOB, LED_Pin, GPIO_PIN_RESET);
        snprintf(response, len, "LED is OFF\r\n");
    } else if (strcasecmp(cmd, "INFO") == 0) {
        snprintf(response, len, "STM32 TCP Server\r\nPort: %d\r\nFirmware: v1.0\r\nStatus: %s\r\n",
                 SERVER_PORT, status);
    } else if (strcasecmp(cmd, "ALARM TEST") == 0) {
        snprintf(response, len, "!!! TEST ALARM ACTIVE !!!\r\n");
        HAL_Delay(200);
        NVIC_SystemReset();
    } else if (strcasecmp(cmd, "HELP") == 0) {
        snprintf(response, len,
                 "Available commands:\r\n"
                 " STATUS / TEMP  - Show temperature and status\r\n"
                 " LED ON / OFF   - Control LED\r\n"
                 " INFO           - Show system info\r\n"
                 " ALARM TEST     - Simulate alarm\r\n"
                 " EXIT           - Close connection\r\n");
    } else if (strcasecmp(cmd, "EXIT") == 0) {
        snprintf(response, len, "Goodbye!\r\n");
    } else {
        snprintf(response, len, "Unknown command: %s\r\n", cmd);
    }
}

// --- tcpip_callback functions ---
void send_reply(void *arg)
{
    tcp_reply_t *msg = (tcp_reply_t*)arg;

    tcp_write(msg->pcb, msg->reply, strlen(msg->reply), TCP_WRITE_FLAG_COPY);
    tcp_output(msg->pcb);

    free(msg);
}

void close_connection(void *arg)
{
    tcp_reply_t *msg = (tcp_reply_t*)arg;
    tcp_close(msg->pcb);
    free(msg);
}

// --- TCP receive callback ---
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p) {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char buffer[MAX_CMD_LEN] = {0};
    size_t offset = 0;
    struct pbuf *q = p;

    while (q && offset < MAX_CMD_LEN - 1) {
        size_t chunk = (q->len < (MAX_CMD_LEN - 1 - offset)) ? q->len : (MAX_CMD_LEN - 1 - offset);
        memcpy(&buffer[offset], q->payload, chunk);
        offset += chunk;
        q = q->next;
    }
    buffer[offset] = 0;

    for (int i = 0; i < offset; i++) {
        if (buffer[i] == '\r' || buffer[i] == '\n') {
            buffer[i] = 0;
            break;
        }
    }

    pbuf_free(p);
    tcp_recved(tpcb, offset);

    tcp_msg_t msg;
    msg.pcb = tpcb;
    strncpy(msg.cmd, buffer, MAX_CMD_LEN-1);
    msg.cmd[MAX_CMD_LEN-1] = 0;

    osMessageQueuePut(tcpQueueHandle, &msg, 0, 0);

    return ERR_OK;
}

// --- TCP accept callback ---
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    LWIP_UNUSED_ARG(arg);
    LWIP_UNUSED_ARG(err);

    tcp_recv(newpcb, tcp_server_recv);
    tcp_nagle_disable(newpcb);
    newpcb->so_options |= SOF_KEEPALIVE;

    const char *msg = "Connected to STM32 TCP server\r\nType HELP for commands\r\n";

    tcp_reply_t *replyMsg = malloc(sizeof(tcp_reply_t));
    replyMsg->pcb = newpcb;
    strncpy(replyMsg->reply, msg, MAX_CMD_LEN-1);
    replyMsg->reply[MAX_CMD_LEN-1] = 0;
    tcpip_callback(send_reply, replyMsg);

    return ERR_OK;
}

// --- FreeRTOS task to process commands ---
void tcpCommandTask(void *argument)
{
    tcp_msg_t msg;
    char reply[MAX_CMD_LEN];

    for(;;) {
        if (osMessageQueueGet(tcpQueueHandle, &msg, NULL, osWaitForever) == osOK) {

            handle_command(msg.cmd, reply, sizeof(reply));

            tcp_reply_t *replyMsg = malloc(sizeof(tcp_reply_t));
            replyMsg->pcb = msg.pcb;
            strncpy(replyMsg->reply, reply, MAX_CMD_LEN-1);
            replyMsg->reply[MAX_CMD_LEN-1] = 0;

            tcpip_callback(send_reply, replyMsg);

            if (strcasecmp(msg.cmd, "EXIT") == 0) {
                tcp_reply_t *closeMsg = malloc(sizeof(tcp_reply_t));
                closeMsg->pcb = msg.pcb;
                closeMsg->reply[0] = 0;
                tcpip_callback(close_connection, closeMsg);
            }
        }
    }
}

// --- TCP server init ---
void tcp_server_init(void)
{
    tcpQueueHandle = osMessageQueueNew(10, sizeof(tcp_msg_t), NULL);

    server_pcb = tcp_new();
    tcp_bind(server_pcb, IP_ADDR_ANY, SERVER_PORT);
    server_pcb = tcp_listen(server_pcb);
    tcp_accept(server_pcb, tcp_server_accept);

    printf("TCP server started on port %d\r\n", SERVER_PORT);

    // Create FreeRTOS task
    osThreadAttr_t tcpTaskAttr = {
        .name = "TCPTask",
        .stack_size = 1024*4,
        .priority = osPriorityNormal
    };
    osThreadNew(tcpCommandTask, NULL, &tcpTaskAttr);
}
