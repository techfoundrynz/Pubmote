#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define CHARGE_ESTIMATOR_VOLTAGE_BINS 26
#define CHARGE_ESTIMATOR_CURRENT_BINS 10
#define CHARGE_ESTIMATOR_CELLS (CHARGE_ESTIMATOR_VOLTAGE_BINS * CHARGE_ESTIMATOR_CURRENT_BINS)
#define CHARGE_ESTIMATOR_VERSION 0x43455431u

  typedef struct {
    uint32_t seconds;
    uint32_t generation;
    uint16_t sessions;
    uint16_t reserved;
  } ChargeEstimateCell;

  typedef struct {
    uint32_t version;
    uint32_t generation;
    ChargeEstimateCell cells[CHARGE_ESTIMATOR_CELLS];
  } ChargeEstimateModel;

  typedef struct {
    uint32_t elapsed_sum;
    uint16_t samples;
    uint16_t reserved;
  } ChargeEstimateObservation;

  typedef struct {
    ChargeEstimateObservation observations[CHARGE_ESTIMATOR_CELLS];
    uint64_t started_ms;
    uint64_t last_sample_ms;
    float voltage_mv;
    float current_ma;
    uint32_t remaining_seconds;
    bool active;
  } ChargeEstimateSession;

  void charge_estimator_model_reset(ChargeEstimateModel *model);
  bool charge_estimator_model_valid(const ChargeEstimateModel *model);
  void charge_estimator_session_reset(ChargeEstimateSession *session);
  // Single owner. True only when a completed charge changed the learned model.
  bool charge_estimator_update(ChargeEstimateModel *model, ChargeEstimateSession *session, uint64_t now_ms,
                               uint16_t voltage_mv, uint16_t current_ma, bool powered, bool charging, bool done,
                               bool fault);

#ifdef __cplusplus
}
#endif
