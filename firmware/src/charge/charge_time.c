#include "charge_time.h"
#include "charge/charge_estimator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "remote/radio_session.h"
#include "remote/settings_store.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-CHARGE_TIME";
static ChargeEstimateModel model;
static ChargeEstimateSession session;
static atomic_uint remaining_seconds = 0;
static bool persistence_enabled = true; // Maintenance worker only.

void charge_time_set_persistence_enabled(bool enabled) {
  persistence_enabled = enabled;
}

static esp_err_t save_profile(void *context) {
  return persistence_enabled ? nvs_write_blob("charge_profile", context, sizeof(model)) : ESP_OK;
}

static void saved_profile(esp_err_t result, void *context) {
  if (result != ESP_OK)
    ESP_LOGW(TAG, "Could not save charging profile: %s", esp_err_to_name(result));
  free(context);
}

void charge_time_init() {
  charge_estimator_model_reset(&model);
  if (nvs_read_blob("charge_profile", &model, sizeof(model)) != ESP_OK || !charge_estimator_model_valid(&model))
    charge_estimator_model_reset(&model);
  charge_estimator_session_reset(&session);
  atomic_store(&remaining_seconds, 0);
}

void charge_time_sample(const RemotePowerState *power) {
  if (charge_estimator_update(&model, &session, (uint64_t)(esp_timer_get_time() / 1000), power->voltage, power->current,
                              power->isPowered, power->chargeState == CHARGE_STATE_CHARGING,
                              power->chargeState == CHARGE_STATE_DONE, power->isFault)) {
    // Save a stable copy on the internal maintenance stack. Only successful
    // charge completion writes flash, and the live model stays power-task-owned.
    ChargeEstimateModel *copy = malloc(sizeof(model));
    if (copy) {
      memcpy(copy, &model, sizeof(model));
      const esp_err_t result = radio_session_run(save_profile, saved_profile, copy);
      if (result != ESP_OK)
        saved_profile(result, copy);
    }
    else {
      ESP_LOGW(TAG, "No memory to save charging profile");
    }
  }
  atomic_store(&remaining_seconds, session.remaining_seconds);
}

uint32_t charge_time_remaining_seconds() {
  return atomic_load(&remaining_seconds);
}
