#include "wifi.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "psa/crypto.h"
#include "shared.h"

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
#define WIFI_TASK_STACK_SIZE 4096

#define PAYLOAD_HEADER_SIZE 22
#define MAX_PAYLOAD_DATA_SIZE (ESP_NOW_MAX_DATA_LEN - PAYLOAD_HEADER_SIZE)
#pragma pack(push, 1)
struct wifi_payload {
  // Start header
  uint32_t sequence;
  uint8_t hmac[16];
  uint16_t data_len;
  // End header
  uint8_t data[MAX_PAYLOAD_DATA_SIZE];
};
#pragma pack(pop)

/** State of wifi connection. */
static struct wifi_state {
  // State of current repeated send.
  uint32_t sequence;
  uint8_t data[MAX_PAYLOAD_DATA_SIZE];
  uint16_t len;
  uint16_t repeat_count;

  // Mutex for repeated send info above.
  SemaphoreHandle_t mutex;
  StaticSemaphore_t mutex_state[1];

  // Task to run the repeat lock.
  TaskHandle_t repeat_task;
  StaticTask_t repeat_task_state[1];
  StackType_t repeat_task_stack[WIFI_TASK_STACK_SIZE];

  /** @brief Shared atomic variables. Contains sequence. */
  struct shared *shared;

  // Callback for handling received data.
  void (*on_receive)(const void *data, uint16_t len);
} wifi_state[1];

static void wifi_send_callback(const esp_now_send_info_t *, esp_now_send_status_t status) {
  ESP_ERROR_CHECK_WITHOUT_ABORT(status);
}

/** @brief Returns a 16-byte hmac based on given message sequence number and data. */
static void get_hmac(uint8_t *hmac, uint32_t sequence, void *data, uint16_t len) {
  static const uint8_t secret_key[16] __attribute__((nonstring)) = "My4secret2key#@!";
  psa_hash_operation_t operation[1] = {{0}};

  psa_status_t status = psa_hash_setup(operation, PSA_ALG_SHA_256);
  if (status != PSA_SUCCESS) goto exit_no_abort;

  status = psa_hash_update(operation, data, len);
  if (status != PSA_SUCCESS) goto exit_with_abort;

  status = psa_hash_update(operation, (const uint8_t *)&sequence, sizeof sequence);
  if (status != PSA_SUCCESS) goto exit_with_abort;

  status = psa_hash_update(operation, secret_key, sizeof secret_key);
  if (status != PSA_SUCCESS) goto exit_with_abort;

  size_t hash_len;
  uint8_t hash[PSA_HASH_LENGTH(PSA_ALG_SHA_256)];
  status = psa_hash_finish(operation, hash, sizeof hash, &hash_len);
  if (status != PSA_SUCCESS) goto exit_with_abort;
  memcpy(hmac, hash, 16);
  return;

exit_with_abort:
  psa_hash_abort(operation);
exit_no_abort:
  ESP_LOGE(tag, "sha256 fail 0x%x", status);
  // Last ditch will let the dimmer keep working if xmit and recv both fail.
  memcpy(hmac, "0123456789abcdef", 16);
}

/** @brief Unwraps a received payload, verifies it, and invokes the user's callback. */
static void wifi_recv_callback(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  assert(PAYLOAD_HEADER_SIZE <= len && len <= UINT16_MAX);     // sanity check
  struct wifi_payload *payload = (struct wifi_payload *)data;  // okay, as data is 32-bit aligned

  // Manage sequence numbers.
  uint32_t state_sequence = get_shared_sequence(wifi_state->shared);
  if (payload->sequence == state_sequence) {
    ESP_LOGD(tag, "rec'v ignore seq@%u", state_sequence);
    return;
  }
  if (payload->sequence < state_sequence) {
    // Reject retrograde sequence in received payload. Probable playback attack.
    ESP_LOGE(tag, "rec'v reject seq@%u/%u", payload->sequence, state_sequence);
    return;
  }
  advance_shared_sequence(wifi_state->shared, state_sequence, payload->sequence);
  // Reject hmac mismatch. Probable spoofing.
  uint8_t hmac[16];
  get_hmac(hmac, payload->sequence, payload->data, payload->data_len);
  if (memcmp(hmac, payload->hmac, sizeof payload->hmac) != 0) {
    ESP_LOGE(tag, "rec'v reject hmac@%u", payload->sequence);
    return;
  }
  ESP_LOGD(tag, "rec'd %ub (seq %u)", len, payload->sequence);
  wifi_state->on_receive(payload->data, (uint16_t)len - PAYLOAD_HEADER_SIZE);
}

static void send(void *data, uint16_t len, uint32_t sequence) {
  struct wifi_payload payload[1];
  if (len == 0 || len > sizeof payload->data) {
    ESP_LOGE(tag, "bad data size=%ub", len);
    return;
  }
  payload->sequence = sequence;
  get_hmac(payload->hmac, sequence, data, len);
  memcpy(payload->data, data, len);
  payload->data_len = len;
  esp_err_t err = esp_now_send(broadcast_mac, (uint8_t *)payload, PAYLOAD_HEADER_SIZE + len);
  if (err == ESP_OK) {
    ESP_LOGD(tag, "sent %ub", len);
  } else {
    ESP_LOGE(tag, "on send - %s", esp_err_to_name(err));
  }
}

void send_repeated(void *data, uint16_t len, uint16_t count) {
  // Start mutex
  xSemaphoreTake(wifi_state->mutex, portMAX_DELAY);
  wifi_state->repeat_count = count;
  memcpy(wifi_state->data, data, len);
  wifi_state->len = len;
  wifi_state->sequence = increment_shared_sequence(wifi_state->shared);
  xSemaphoreGive(wifi_state->mutex);
  // End mutex

  xTaskAbortDelay(wifi_state->repeat_task);  // Cancel vTaskDelay(), if any.
  xTaskNotify(wifi_state->repeat_task, 1, eSetValueWithoutOverwrite);  // Wake up!
}

/** @brief The worker task for send_repeated(). */
static void repeat_task(void *parameters) {
  struct wifi_state *wifi_state = parameters;
  for (;;) {
    // Start mutex
    xSemaphoreTake(wifi_state->mutex, portMAX_DELAY);
    if (wifi_state->repeat_count == 0) {
      // End mutex
      xSemaphoreGive(wifi_state->mutex);
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Wait forever for a wakeup.
      continue;
    }
    --wifi_state->repeat_count;
    // Copy data for sending outside mutex.
    uint16_t len = wifi_state->len;
    uint8_t data[wifi_state->len];
    memcpy(data, wifi_state->data, wifi_state->len);
    uint32_t sequence = wifi_state->sequence;
    xSemaphoreGive(wifi_state->mutex);
    // End mutex

    send(data, len, sequence);
    vTaskDelay(SEND_INTERVAL_MS / portTICK_PERIOD_MS);  // Abortable by send_repeated()
  }
}

/**
 * @brief Configures wifi as a singleton.
 *
 * Calls after the first do nothing to the wifi hardware.
 */
void initialize_wifi(struct shared *shared, void (*on_receive)(const void *data, uint16_t len)) {
  // Non-threadsafe assignments are okay here.
  wifi_state->shared = shared;
  wifi_state->on_receive = on_receive;
  if (wifi_state->mutex) {
    return;
  }
  if (psa_crypto_init() != PSA_SUCCESS) {
    ESP_LOGE(tag, "PSA Crypto init fail");
    return;
  }
  // Begin threadsafe operations required.
  wifi_state->mutex = xSemaphoreCreateMutexStatic(wifi_state->mutex_state);
  wifi_state->repeat_task = xTaskCreateStaticPinnedToCore(
      repeat_task, "wifi_repeat", WIFI_TASK_STACK_SIZE,
      wifi_state,  // parameters
      5,           // priority
      wifi_state->repeat_task_stack, wifi_state->repeat_task_state, 1);

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
