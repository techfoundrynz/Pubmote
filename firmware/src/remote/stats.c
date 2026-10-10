#include "remote/stats.h"
#include "remote/settings.h"
#include "remote/stats_lifecycle.h"

void stats_init(void) {
  PairedDevice device;
  int index = get_default_device_index();
  stats_reset(index >= 0 && get_paired_device(index, &device) ? device.vehicle_type : VEHICLE_TYPE_UNSPECIFIED);
  stats_update();
}
