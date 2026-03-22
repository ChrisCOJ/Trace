#include <stdint.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_log.h"

#include "../include/pos_client.h"
#include "../include/trace_system.h"
#include "../include/table_fsm.h"


#define WIFI_SSID           "56ws-guest" // "Deco Wi-Fi"
#define WIFI_PASSWORD       "goodwine@56!" // "Gx9qjrzw9Tfj"
#define SERVER_IP           "10.10.10.188" // "192.168.68.109"
#define SERVER_PORT         5050

#define RECONNECT_DELAY_MS  30000
#define EVENT_QUEUE_LEN     16

#define WIFI_GOT_IP_BIT     BIT0


static const char *TAG = "pos_client";

static EventGroupHandle_t s_wifi_event_group;
static QueueHandle_t      s_event_queue;


static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_GOT_IP_BIT);
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP acquired: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_GOT_IP_BIT);
    }
}


static void wifi_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,      wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,   IP_EVENT_STA_GOT_IP,   wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* sleep between DTIM beacons, saves battery */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
}


static void pos_receive_task(void *arg) {
    (void)arg;

    static const fsm_transition_event event_map[] = {
        [POS_CUSTOMERS_SEATED] = EVENT_CUSTOMERS_SEATED,
        [POS_ORDER_READY]      = EVENT_POS_ORDER_READY,
        [POS_BILL_REQUESTED]   = EVENT_TABLE_REQUESTED_BILL,
    };

    while (1) {
        xEventGroupWaitBits(s_wifi_event_group, WIFI_GOT_IP_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) {
            ESP_LOGE(TAG, "socket() failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            continue;
        }

        struct sockaddr_in server_addr = {
            .sin_family = AF_INET,
            .sin_port   = htons(SERVER_PORT),
        };
        inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

        ESP_LOGI(TAG, "Connecting to %s:%d", SERVER_IP, SERVER_PORT);

        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
            ESP_LOGW(TAG, "connect() failed: errno %d. Retrying in %ds", errno, RECONNECT_DELAY_MS / 1000);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            continue;
        }

        ESP_LOGI(TAG, "Connected to POS server");

        while (1) {
            pos_message msg;
            int received = recv(sock, &msg, sizeof(msg), MSG_WAITALL);

            if (received != (int)sizeof(msg)) {
                if (received == 0) {
                    ESP_LOGW(TAG, "Server closed connection. Retrying in %ds", RECONNECT_DELAY_MS / 1000);
                } else {
                    ESP_LOGE(TAG, "recv() error: errno %d (got %d bytes). Retrying in %ds",
                             errno, received, RECONNECT_DELAY_MS / 1000);
                }
                break;
            }

            if (msg.type > POS_BILL_REQUESTED || msg.table_index >= MAX_TABLES) {
                ESP_LOGW(TAG, "Dropped invalid message: type=%u table=%u", msg.type, msg.table_index);
                continue;
            }

            if (xQueueSend(s_event_queue, &msg, 0) != pdTRUE) {
                ESP_LOGW(TAG, "Event queue full, dropping message: type=%u table=%u", msg.type, msg.table_index);
            }
        }

        close(sock);
        vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
    }
}


void pos_client_start(void) {
    s_wifi_event_group = xEventGroupCreate();
    s_event_queue      = xQueueCreate(EVENT_QUEUE_LEN, sizeof(pos_message));

    wifi_init();

    xTaskCreate(pos_receive_task, "pos_recv", 4096, NULL, 4, NULL);
}


void pos_client_drain_events(time_ms current_time_ms) {
    static const fsm_transition_event event_map[] = {
        [POS_CUSTOMERS_SEATED] = EVENT_CUSTOMERS_SEATED,
        [POS_ORDER_READY]      = EVENT_POS_ORDER_READY,
        [POS_BILL_REQUESTED]   = EVENT_TABLE_REQUESTED_BILL,
    };

    pos_message msg;
    while (xQueueReceive(s_event_queue, &msg, 0) == pdTRUE) {
        fsm_transition_event ev = event_map[msg.type];
        ESP_LOGI(TAG, "Applying POS event: type=%u table=%u", msg.type, msg.table_index);
        system_apply_table_fsm_event(msg.table_index, ev, current_time_ms);
    }
}
