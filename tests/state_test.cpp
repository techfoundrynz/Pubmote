#include "remote/settings_state.h"
#include "remote/stats.h"
#include <atomic>
#include <cassert>
#include <cmath>
#include <thread>

static unsigned notifications;
static void observer() {
  ++notifications;
  // Reentrant reads, writes and removal must not deadlock notification dispatch.
  stats_set_signal_strength(stats_snapshot().signalStrength + 1);
  stats_unregister_update_cb(observer);
}

static void test_notifications_and_reset() {
  stats_publish_power(4100, 90, CHARGE_STATE_CHARGING, 200);
  stats_set_vehicle_type(VEHICLE_TYPE_ESKATE);
  BoardTelemetry packet = {};
  packet.speed = 12;
  packet.state = BOARD_STATE_RUNNING;
  stats_publish_board(&packet, 42);
  auto value = stats_snapshot();
  assert(value.remoteBatteryVoltage == 4100 && value.chargeCurrent == 200);
  assert(value.vehicleType == VEHICLE_TYPE_ESKATE && value.lastUpdated == 42);
  assert(!stats_register_update_cb(nullptr));
  assert(stats_register_update_cb(observer));
  assert(stats_register_update_cb(observer));
  stats_update();
  stats_update();
  assert(notifications == 1);
  stats_reset(VEHICLE_TYPE_ONEWHEEL);
  value = stats_snapshot();
  assert(value.lastUpdated == 0 && value.speed == 0 && value.signalStrength == -255);
  assert(value.remoteBatteryVoltage == 4100 && value.remoteBatteryPercentage == 90);
  assert(value.chargeState == CHARGE_STATE_UNKNOWN && value.chargeCurrent == 0);
  assert(value.vehicleType == VEHICLE_TYPE_ONEWHEEL);
}

static void test_settings_and_pairing() {
  SettingsSnapshot initial = {};
  initial.pairing.default_index = -1;
  initial.device.theme_color = 0x123456;
  settings_state_init(&initial);
  DeviceSettings patch = {};
  patch.bl_level = 123;
  patch.screen_rotation = SCREEN_ROTATION_90;
  assert(settings_update_device(&patch, SETTINGS_DEVICE_BL_LEVEL | SETTINGS_DEVICE_SCREEN_ROTATION));
  assert(settings_get_device().theme_color == 0x123456);
  patch.screen_rotation = SCREEN_ROTATION_COUNT;
  patch.bl_level = 10;
  assert(!settings_update_device(&patch, SETTINGS_DEVICE_BL_LEVEL | SETTINGS_DEVICE_SCREEN_ROTATION));
  assert(settings_get_device().bl_level == 123);
  assert(!settings_update_device(&patch, 1u << 31));
  ImuCalibrationSettings imu = {};
  imu.accel_x_offset = NAN;
  assert(!settings_set_imu(&imu));
  assert(settings_get_imu().accel_x_offset == 0);
  const uint8_t first[6] = {1, 2, 3, 4, 5, 6};
  const uint8_t second[6] = {2, 3, 4, 5, 6, 7};
  settings_set_peer(first, 7);
  settings_set_pairing_secret(1234);
  settings_remember_active_peer();
  assert(settings_get_pairing().device_count == 1);
  assert(settings_set_saved_vehicle(first, VEHICLE_TYPE_ONEWHEEL));
  settings_set_peer(second, 0x81);
  settings_set_pairing_secret(5678);
  settings_remember_active_peer();
  assert(settings_select_device(0));
  auto peer = settings_get_pairing();
  assert(peer.channel == 7 && peer.secret_code == 1234 && peer.devices[0].vehicle_type == VEHICLE_TYPE_ONEWHEEL);
  assert(settings_set_saved_secret(4321));
  assert(settings_get_pairing().devices[0].secret_code == 4321);
  assert(settings_remove_device(0));
  peer = settings_get_pairing();
  assert(peer.device_count == 1 && peer.default_index == 0 && peer.secret_code == 5678 && peer.channel == 0x81);
  assert(settings_remove_device(0));
  peer = settings_get_pairing();
  assert(peer.default_index == -1 && peer.secret_code == UINT32_MAX && peer.remote_addr[0] == 0xff);
  assert(!settings_select_device(0) && !settings_remove_device(0));
}

static void test_concurrent_snapshots() {
  constexpr unsigned iterations = 30000;
  stats_reset(0);
  stats_publish_power(0, 0, CHARGE_STATE_UNKNOWN, 0);
  SettingsSnapshot initial = {};
  initial.pairing.default_index = -1;
  settings_state_init(&initial);
  std::atomic<unsigned> running{3};
  std::thread board([&]() {
    for (unsigned i = 1; i <= iterations; ++i) {
      BoardTelemetry packet = {};
      packet.speed = float(i);
      packet.batteryVoltage = float(i);
      packet.tripDistance = float(i);
      stats_publish_board(&packet, i);
      settings_set_theme_color(i);
    }
    --running;
  });
  std::thread power([&]() {
    for (unsigned i = 1; i <= iterations; ++i) {
      stats_publish_power(uint16_t(i), uint8_t(i % 101), CHARGE_STATE_CHARGING, uint16_t(i));
      settings_set_bl_level(uint8_t(i % 256));
    }
    --running;
  });
  std::thread input([&]() {
    for (unsigned i = 1; i <= iterations; ++i) {
      InputPinSettings pins = {};
      CalibrationSettings calibration = {};
      pins.js_x_gpio = int8_t(i % 40);
      calibration.x_center = uint16_t(i % 40);
      settings_publish_input(&pins, &calibration);
    }
    --running;
  });
  do {
    auto telemetry = stats_snapshot();
    assert(telemetry.speed == float(telemetry.lastUpdated));
    assert(telemetry.speed == telemetry.batteryVoltage && telemetry.speed == telemetry.tripDistance);
    assert(telemetry.remoteBatteryVoltage == telemetry.chargeCurrent);
    assert(telemetry.remoteBatteryPercentage == telemetry.remoteBatteryVoltage % 101);
    auto settings = settings_snapshot();
    assert(settings.pins.js_x_gpio == settings.calibration.x_center);
  } while (running);
  board.join();
  power.join();
  input.join();
  auto device = settings_get_device();
  assert(device.theme_color == iterations && device.bl_level == iterations % 256);
}

int main() {
  test_notifications_and_reset();
  test_settings_and_pairing();
  test_concurrent_snapshots();
}
