#include "shared.h"

#include "esp_log.h"

#define NAMESPACE "dimmer"
#define KEY "ckpt-v1"
#define UPDATE_DEBOUNCE_MS 800

static const char tag[] = "shared";

/** @brief Saves the given data to non-volatile storage and updates the register. */
static void save_checkpoint(struct shared *shared, struct checkpoint *checkpoint) {
  // Skip save if we'd be overwriting same value.
  if (memcmp(shared->checkpoint, checkpoint, sizeof *checkpoint) == 0) {
    return;
  }
  if (nvs_set_blob(shared->nvs, KEY, checkpoint, sizeof *checkpoint) != ESP_OK ||
      nvs_commit(shared->nvs) != ESP_OK) {
    ESP_LOGE(tag, "chkpt fail: seq=%u, lvl=%u", checkpoint->sequence, checkpoint->level_mils);
    return;
  }
  ESP_LOGD(tag, "chkpt: seq=%u, lvl=%u", checkpoint->sequence, checkpoint->level_mils);
  *shared->checkpoint = *checkpoint;
}

/** @brief Restores the saved data register from non-volatile storage.  */
static bool restore_checkpoint(struct shared *shared) {
  size_t len = sizeof *shared->checkpoint;
  if (nvs_get_blob(shared->nvs, KEY, shared->checkpoint, &len) != ESP_OK ||
      len != sizeof *shared->checkpoint) {
    // Something's wonky. Clear the namespace to start over.
    nvs_erase_all(shared->nvs);
    nvs_commit(shared->nvs);
    return false;
  }
  return true;
}

/** Copy atomic shared data to given in-memory checkpoint. */
static void fetch_checkpoint(struct shared *shared, struct checkpoint *checkpoint) {
  checkpoint->sequence = shared->sequence;
  checkpoint->level_mils = shared->level_mils;
}

static void checkpoint_task(void *parameters) {
  struct shared *shared = parameters;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // To avoid wearing out flash memory, write NVS only after data are no
    // longer changing within the debounce period. Decide via snapshot, wait,
    // and re-fetch to see whether anything changed. If so, the re-fetch is
    // now the snapshot. Rinse and repeat.
    struct checkpoint snapshot[1], refetch[1];
    fetch_checkpoint(shared, snapshot);
    for (;;) {
      vTaskDelay(UPDATE_DEBOUNCE_MS / portTICK_PERIOD_MS);
      fetch_checkpoint(shared, refetch);
      if (memcmp(snapshot, refetch, sizeof *refetch) == 0) {
        // Debounce is complete.
        save_checkpoint(shared, snapshot);
        break;
      }
      *snapshot = *refetch;
    }
  }
}

void initialize_shared(struct shared *shared) {
  shared->checkpoint_task = NULL;
  ESP_ERROR_CHECK(nvs_open(NAMESPACE, NVS_READWRITE, &shared->nvs));
  if (!restore_checkpoint(shared)) {
    memset(shared->checkpoint, 0, sizeof *shared->checkpoint);
  }
  shared->sequence = shared->checkpoint->sequence;
  shared->level_mils = shared->checkpoint->level_mils;
  ESP_LOGI(tag, "restore - seq=%u, lvl=%u", shared->sequence, shared->level_mils);
  shared->checkpoint_task = xTaskCreateStaticPinnedToCore(
      checkpoint_task, "checkpoint", CHECKPOINT_TASK_STACK_SIZE,
      shared,  // parameters
      5,       // priority
      shared->checkpoint_task_stack, shared->checkpoint_task_state, 1);
}

void checkpoint(struct shared *shared) { xTaskNotify(shared->checkpoint_task, 1, eSetBits); }

void advance_shared_sequence(struct shared *shared, uint32_t from, uint32_t to) {
  while (to > from && !atomic_compare_exchange_weak_explicit(
                          &shared->sequence, &from, to, memory_order_relaxed, memory_order_relaxed))
    /* skip */;
}

bool set_shared_level_mils(struct shared *shared, int32_t to_level_mils) {
  if (to_level_mils < 0) {
    to_level_mils = 0;
  } else if (to_level_mils > 1024) {
    to_level_mils = 1024;
  }
  uint16_t from_level_mils = shared->level_mils;
  while (from_level_mils != to_level_mils) {
    if (atomic_compare_exchange_weak_explicit(&shared->level_mils, &from_level_mils, to_level_mils,
                                              memory_order_relaxed, memory_order_relaxed)) {
      return true;
    }
  }
  return false;
}
