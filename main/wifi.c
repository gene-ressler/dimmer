#include "wifi.h"

#include <assert.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

static const char tag[] = "wifi";

void get_mac(uint8_t *mac, char *text) {
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char *sep = "";
  for (int32_t i = 0, p = 0; i < 6; ++i) {
    p += sprintf(text + p, "%s%02x", sep, mac[i]);
    sep = ":";
  }
}

static const uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#define WIFI_CHANNEL 3
#define SEND_INTERVAL_MS 2000
#define TASK_STACK_SIZE 4096

#define PAYLOAD_HEADER_SIZE 20
#define MAX_PAYLOAD_DATA_SIZE (ESP_NOW_MAX_DATA_LEN - PAYLOAD_HEADER_SIZE)
#pragma pack(push, 1)
struct wifi_payload {
  uint32_t sequence;
  uint8_t signature[16];
  uint8_t data[MAX_PAYLOAD_DATA_SIZE];
};
#pragma pack(pop)

/** State of wifi connection. */
static struct wifi_state {
  // State of current repeated send.
  uint8_t data[MAX_PAYLOAD_DATA_SIZE];
  uint16_t len;
  uint16_t repeat_count;

  // Mutex for repeated send info above.
  SemaphoreHandle_t mutex;
  StaticSemaphore_t mutex_state[1];

  // Task to run the repeat lock.
  TaskHandle_t repeat_task;
  StaticTask_t repeat_task_state[1];
  StackType_t repeat_task_stack[TASK_STACK_SIZE];

  // Callback for handling received data.
  void (*on_receive)(const void *data, uint16_t len);
} wifi_state[1];

static void wifi_send_callback(const esp_now_send_info_t *, esp_now_send_status_t status) {
  ESP_ERROR_CHECK_WITHOUT_ABORT(status);
}

/** @brief Unwraps a received payload, verifies it, and invokes the user's callback. */
static void wifi_recv_callback(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  assert(PAYLOAD_HEADER_SIZE <= len && len <= UINT16_MAX);     // sanity check
  struct wifi_payload *payload = (struct wifi_payload *)data;  // okay, as data is 32-bit aligned
  // TODO: Add signature check.
  ESP_LOGI(tag, "rec'd %ub (seq %u)", len, payload->sequence);
  wifi_state->on_receive(payload->data, (uint16_t)len - PAYLOAD_HEADER_SIZE);
}

/** @brief Wraps given data in a payload and broadcasts it. */
void send(void *data, uint16_t len) {
  struct wifi_payload payload[1];
  if (len == 0 || len > sizeof payload->data) {
    ESP_LOGE(tag, "bad data size=%ub", len);
    return;
  }
  // TODO: Add signature.
  payload->sequence = 0x424242;
  memcpy(payload->signature, "0123456789abcdef", sizeof payload->signature);
  memcpy(payload->data, data, len);
  esp_err_t err = esp_now_send(broadcast_mac, (uint8_t *)payload, PAYLOAD_HEADER_SIZE + len);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "on send - %s", esp_err_to_name(err));
  }
  ESP_LOGI(tag, "sent %ub", len);
  ESP_LOG_BUFFER_HEX_LEVEL(tag, data, len, ESP_LOG_INFO);
  ESP_LOGI(tag, "payload");
  ESP_LOG_BUFFER_HEX_LEVEL(tag, payload, PAYLOAD_HEADER_SIZE + len, ESP_LOG_INFO);
}

/** Sends the given message repeatedly. */
void send_repeated(void *data, uint16_t len, uint16_t count) {
  // Start mutex
  xSemaphoreTake(wifi_state->mutex, portMAX_DELAY);
  wifi_state->repeat_count = count;
  memcpy(wifi_state->data, data, len);
  wifi_state->len = len;
  xSemaphoreGive(wifi_state->mutex);
  // End mutex
  xTaskAbortDelay(wifi_state->repeat_task);  // Cancel vTaskDelay(), if any.
  xTaskNotify(wifi_state->repeat_task, 1, eSetValueWithoutOverwrite);  // Wake up!
}

/** The worker task for send_repeated(). */
static void repeat_task(void *parameters) {
  struct wifi_state *state = parameters;
  for (;;) {
    // Start mutex
    xSemaphoreTake(state->mutex, portMAX_DELAY);
    if (state->repeat_count == 0) {
      xSemaphoreGive(state->mutex);
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Wait forever for a wakeup.
      continue;
    }
    --state->repeat_count;
    // Copy data for sending outside mutex.
    uint16_t len = state->len;
    uint8_t data[state->len];
    memcpy(data, state->data, state->len);
    xSemaphoreGive(state->mutex);
    // End mutex
    send(data, len);
    vTaskDelay(SEND_INTERVAL_MS / portTICK_PERIOD_MS);  // Abortable by send_repeated()
  }
}

/**
 * @brief Configures wifi as a singleton.
 *
 * Calls after the first do nothing except reset the on_receive handler.
 */
void configure_wifi(void (*on_receive)(const void *data, uint16_t len)) {
  wifi_state->on_receive = on_receive;
  if (wifi_state->mutex) {
    return;
  }
  wifi_state->mutex = xSemaphoreCreateMutexStatic(wifi_state->mutex_state);
  wifi_state->repeat_task =
      xTaskCreateStatic(repeat_task, "wifi_repeat", TASK_STACK_SIZE,
                        wifi_state,  // parameters
                        5,           // priority
                        wifi_state->repeat_task_stack, wifi_state->repeat_task_state);

  // Non-volatile storage
  ESP_ERROR_CHECK(nvs_flash_init());

  // Wifi
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  wifi_init_config_t cfg[1] = {WIFI_INIT_CONFIG_DEFAULT()};
  ESP_ERROR_CHECK(esp_wifi_init(cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));

  // Esp-now
  ESP_ERROR_CHECK(esp_now_init());

  // Register callbacks
  ESP_ERROR_CHECK(esp_now_register_send_cb(wifi_send_callback));
  ESP_ERROR_CHECK(esp_now_register_recv_cb(wifi_recv_callback));

  // Broadcast peer
  esp_now_peer_info_t peer_info = {0};
  memcpy(peer_info.peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
  peer_info.channel = 0;  // Use current Wi-Fi channel
  peer_info.ifidx = WIFI_IF_STA;
  peer_info.encrypt = false;  // Broadcast packets cannot be encrypted

  ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));
}
