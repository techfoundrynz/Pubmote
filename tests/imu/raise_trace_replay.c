#include "imu/raise_detector.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// Reads full ESP-IDF monitor logs or the CSV records alone. Gyro values and
// recorded state are accepted for diagnostics but do not drive the detector.
int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: raise_trace_replay <monitor-log>\n");
    return 2;
  }
  FILE *input = fopen(argv[1], "r");
  if (!input)
    return 2;
  raise_detector_t state;
  raise_detector_reset(&state);
  char line[1024];
  unsigned count = 0;
  uint64_t previous_ms = 0;
  puts("ms,accel_x,accel_y,accel_z,recorded_raised,replayed_raised");
  while (fgets(line, sizeof(line), input)) {
    char *record = strstr(line, "raise_trace,");
    if (!record)
      continue;
    uint64_t ms;
    float ax, ay, az, gx, gy, gz;
    int valid, gyro_valid, recorded;
    if (sscanf(record, "raise_trace,%" SCNu64 ",%f,%f,%f,%f,%f,%f,%d,%d,%d", &ms, &ax, &ay, &az, &gx, &gy, &gz, &valid,
               &gyro_valid, &recorded) != 10 ||
        (count && ms <= previous_ms)) {
      fprintf(stderr, "Malformed or non-monotonic trace; split logs at reboot.\n");
      fclose(input);
      return 1;
    }
    previous_ms = ms;
    bool raised = raise_detector_update(&state, ms, ax, ay, az, valid != 0);
    printf("%" PRIu64 ",%.4f,%.4f,%.4f,%d,%d\n", ms, ax, ay, az, recorded, raised);
    ++count;
  }
  fclose(input);
  if (!count) {
    fprintf(stderr, "No raise_trace records found.\n");
    return 1;
  }
  return 0;
}
