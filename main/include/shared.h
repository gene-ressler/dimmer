/**
 * @file
 * @brief Multi-thread shared and/or checkpointed state.
 */
#pragma once

#include <stdatomic.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

#define CHECKPOINT_TASK_STACK_SIZE 2048  ///< Size of the checkpoint task's stack.

/** @brief Persistent checkpoint. */
struct checkpoint {
  uint32_t sequence;    ///< Broadcast sequence number.
  uint16_t level_mils;  ///< Dimming level in [0..1024].
};

/** @brief State shared among threads and/or checkpointed to persistent store. */
struct shared {
  // Shared data.
  _Atomic uint32_t sequence;    ///< Broadcast sequence number.
  _Atomic uint16_t level_mils;  ///< Dimming level in [0..1024].

  struct checkpoint checkpoint[1];  ///< The last data saved as a checkpoint.

  nvs_handle_t nvs;  ///< Non-volatile storage.

  TaskHandle_t checkpoint_task;           ///< Checkpoint manager task.
  StaticTask_t checkpoint_task_state[1];  ///< Checkpoint manager task state.
  StackType_t checkpoint_task_stack[CHECKPOINT_TASK_STACK_SIZE];  ///< Checkpoint manager stack.
};

/** @brief Initializes at last checkpoint, if any, and starts the checkpoint task. */
void initialize_shared(struct shared *shared);

/** @brief Gets last-used sequence value or zero if none yet has. Thread safe. */
inline uint32_t get_shared_sequence(struct shared *shared) { return shared->sequence; }

/** @brief Sets last-seen sequence value. Thread safe. */
inline void set_shared_sequence(struct shared *shared, uint32_t sequence) {
  shared->sequence = sequence;
}

/** @brief Gets the next available sequence value. Thread safe. */
inline uint32_t increment_shared_sequence(struct shared *shared) { return ++shared->sequence; }

/** @brief Advances the shared sequence to a new value if it's greater. Thread safe. */
void advance_shared_sequence(struct shared *shared, uint32_t from, uint32_t to);

/** @brief Gets the current level value. Thread safe. */
inline uint16_t get_shared_level_mils(struct shared *shared) { return shared->level_mils; }

/** @brief Sets level clamped to [0..1024], returning whether a change resulted. Thread safe. */
bool set_shared_level_mils(struct shared *shared, int32_t level_mils);

/** @brief Causes a checkpoint to be recorded asynchronously with debouncing. Thread safe. */
void checkpoint(struct shared *shared);
