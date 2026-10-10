#pragma once
#include "remote/telemetry_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef enum {
    SPEED_UNIT_KMH,
    SPEED_UNIT_MPH
  } SpeedUnit;

  typedef enum {
    TEMP_UNIT_CELSIUS,
    TEMP_UNIT_FAHRENHEIT
  } TempUnit;

  typedef struct {
    int64_t lastUpdated;
    /* Stats */
    // Speed, stored in KPH
    float speed;
    // Max speed, stored in KPH
    float maxSpeed;
    // 0 to 100
    uint8_t dutyCycle;
    // -100 to 100, motor current as a share of the ESC limit, negative = braking
    int8_t phaseUtilization;
    // -100 to 100, battery current as a share of the ESC limit, negative = regen
    int8_t batteryUtilization;
    // Unit of speed measure
    SpeedUnit speedUnit;
    // Unit of temperature measure
    TempUnit tempUnit;
    // Board battery voltage
    float batteryVoltage;
    // 0 to 100
    uint8_t batteryPercentage;
    // Remote battery voltage
    uint16_t remoteBatteryVoltage;
    // 0 to 100
    uint8_t remoteBatteryPercentage;
    RemoteChargeState chargeState; // Current charge state
    uint16_t chargeCurrent;        // Charge current in mA
    // Board trip distance
    float tripDistance;
    // Board motor temperature
    float motorTemp;
    // Board controller temperature
    float controllerTemp;
    // RSSI
    int signalStrength;
    // Main board state
    BoardState state;
    // Footpad switch state
    SwitchState switchState;
    // Vehicle type
    uint8_t vehicleType;
  } RemoteStats;

  typedef enum {
    VEHICLE_TYPE_UNSPECIFIED = 0,
    VEHICLE_TYPE_ONEWHEEL = 1,
    VEHICLE_TYPE_ESKATE = 2,
    VEHICLE_TYPE_SCOOTER = 3,
    VEHICLE_TYPE_EUC = 4
  } VehicleType;

  typedef void (*stats_update_callback_t)(void);

  // Each read is a coherent copy. Publishers modify only their own field group.
  RemoteStats stats_snapshot(void);
  void stats_publish_board(const BoardTelemetry *reading, int64_t timestamp);
  void stats_publish_power(uint16_t voltage, uint8_t percentage, RemoteChargeState state, uint16_t current);
  void stats_set_signal_strength(int rssi);
  void stats_set_vehicle_type(uint8_t vehicle_type);
  void stats_set_duty_cycle(uint8_t duty_cycle);
  // Highest of duty, |phase| and |battery| utilization, 0 to 100
  uint8_t stats_utilization(const RemoteStats *stats);
  void stats_reset(uint8_t vehicle_type);

  void stats_update();
  // Unique registrations; returns false for NULL or when the 16 slots are full.
  bool stats_register_update_cb(stats_update_callback_t callback);
  void stats_unregister_update_cb(stats_update_callback_t callback);

#ifdef __cplusplus
}
#endif
