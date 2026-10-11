#include "charge/charge_estimator.h"
#include <string.h>

#define MAX_SESSION_SECONDS (12 * 60 * 60)
#define MAX_SAMPLE_GAP_MS 10000
#define SETTLE_MS 15000
#define HISTORY_SESSIONS 4
#define MAX_HISTORY_AGE 8

void charge_estimator_model_reset(ChargeEstimateModel *model) {
  memset(model, 0, sizeof(*model));
  model->version = CHARGE_ESTIMATOR_VERSION;
}

bool charge_estimator_model_valid(const ChargeEstimateModel *model) {
  if (model->version != CHARGE_ESTIMATOR_VERSION)
    return false;
  for (unsigned i = 0; i < CHARGE_ESTIMATOR_CELLS; ++i) {
    const ChargeEstimateCell *cell = &model->cells[i];
    if (cell->sessions > HISTORY_SESSIONS || cell->generation > model->generation ||
        cell->seconds > MAX_SESSION_SECONDS || (!cell->sessions && cell->seconds) ||
        (cell->sessions && (!cell->seconds || !cell->generation)))
      return false;
  }
  return true;
}

void charge_estimator_session_reset(ChargeEstimateSession *session) {
  memset(session, 0, sizeof(*session));
}

static unsigned cell_index(float voltage, float current) {
  unsigned voltage_bin = (unsigned)((voltage - 3000.0f + 25.0f) / 50.0f);
  if (voltage_bin >= CHARGE_ESTIMATOR_VOLTAGE_BINS)
    voltage_bin = CHARGE_ESTIMATOR_VOLTAGE_BINS - 1;
  static const unsigned thresholds[] = {80, 130, 180, 260, 380, 550, 800, 1200, 2000};
  unsigned current_bin = 0;
  while (current_bin < CHARGE_ESTIMATOR_CURRENT_BINS - 1 && current >= thresholds[current_bin])
    ++current_bin;
  return voltage_bin * CHARGE_ESTIMATOR_CURRENT_BINS + current_bin;
}

static bool relevant(const ChargeEstimateModel *model, const ChargeEstimateCell *cell) {
  return cell->sessions && model->generation - cell->generation <= MAX_HISTORY_AGE;
}

static bool finish_session(ChargeEstimateModel *model, const ChargeEstimateSession *session, uint32_t duration) {
  unsigned observations = 0, compared = 0, changed = 0;
  for (unsigned i = 0; i < CHARGE_ESTIMATOR_CELLS; ++i) {
    const ChargeEstimateObservation *sample = &session->observations[i];
    if (sample->samples < 5)
      continue;
    ++observations;
    const uint32_t seconds = duration - sample->elapsed_sum / sample->samples;
    const ChargeEstimateCell *cell = &model->cells[i];
    if (!relevant(model, cell) || seconds < 60 || cell->seconds < 60)
      continue;
    ++compared;
    if ((uint64_t)seconds * 10 > (uint64_t)cell->seconds * 16 || (uint64_t)seconds * 16 < (uint64_t)cell->seconds * 10)
      ++changed;
  }
  if (!observations)
    return false;
  // A consistently different profile can indicate a replacement battery. Drop
  // uncovered old ranges too, rather than mixing two different batteries.
  if ((compared >= 3 && changed * 4 >= compared * 3) || model->generation == UINT32_MAX)
    charge_estimator_model_reset(model);
  ++model->generation;
  for (unsigned i = 0; i < CHARGE_ESTIMATOR_CELLS; ++i) {
    const ChargeEstimateObservation *sample = &session->observations[i];
    if (sample->samples < 5)
      continue;
    ChargeEstimateCell *cell = &model->cells[i];
    const uint32_t seconds = duration - sample->elapsed_sum / sample->samples;
    const unsigned weight = relevant(model, cell) ? cell->sessions : 0;
    cell->seconds = (cell->seconds * weight + seconds) / (weight + 1);
    cell->sessions = weight < HISTORY_SESSIONS ? weight + 1 : HISTORY_SESSIONS;
    cell->generation = model->generation;
  }
  return true;
}

bool charge_estimator_update(ChargeEstimateModel *model, ChargeEstimateSession *session, uint64_t now_ms,
                             uint16_t voltage_mv, uint16_t current_ma, bool powered, bool charging, bool done,
                             bool fault) {
  session->remaining_seconds = 0;
  const bool valid_voltage = voltage_mv >= 3000 && voltage_mv <= 4350;
  if (!powered || fault || !valid_voltage || (session->active && now_ms < session->last_sample_ms)) {
    charge_estimator_session_reset(session);
    return false;
  }
  if (done) {
    const uint64_t duration = session->active ? (now_ms - session->started_ms) / 1000 : 0;
    const bool learned = session->active && now_ms - session->last_sample_ms <= MAX_SAMPLE_GAP_MS && duration >= 60 &&
                         duration <= MAX_SESSION_SECONDS && finish_session(model, session, (uint32_t)duration);
    charge_estimator_session_reset(session);
    return learned;
  }
  if (!charging || current_ma < 25 || current_ma > 5056) {
    charge_estimator_session_reset(session);
    return false;
  }
  if (!session->active || now_ms - session->last_sample_ms > MAX_SAMPLE_GAP_MS) {
    charge_estimator_session_reset(session);
    session->active = true;
    session->started_ms = session->last_sample_ms = now_ms;
    session->voltage_mv = voltage_mv;
    session->current_ma = current_ma;
    return false;
  }
  const uint64_t elapsed_ms = now_ms - session->started_ms;
  if (elapsed_ms / 1000 > MAX_SESSION_SECONDS) {
    charge_estimator_session_reset(session);
    return false;
  }
  const uint64_t dt = now_ms - session->last_sample_ms;
  if (dt >= 1000) {
    const float voltage_alpha = (float)dt / (5000.0f + dt);
    const float current_alpha = (float)dt / (10000.0f + dt);
    session->voltage_mv += voltage_alpha * (voltage_mv - session->voltage_mv);
    session->current_ma += current_alpha * (current_ma - session->current_ma);
    session->last_sample_ms = now_ms;
    if (elapsed_ms >= SETTLE_MS) {
      ChargeEstimateObservation *observation =
          &session->observations[cell_index(session->voltage_mv, session->current_ma)];
      observation->elapsed_sum += (uint32_t)(elapsed_ms / 1000);
      ++observation->samples;
    }
  }
  if (elapsed_ms >= SETTLE_MS) {
    const ChargeEstimateCell *cell = &model->cells[cell_index(session->voltage_mv, session->current_ma)];
    if (relevant(model, cell))
      session->remaining_seconds = cell->seconds;
  }
  return false;
}
