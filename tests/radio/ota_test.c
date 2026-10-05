#include "common.h"
#define ESP_ERR_INVALID_RESPONSE -20
#define ESP_ERR_HTTP_EAGAIN 21
#define ESP_ERR_OTA_VALIDATE_FAILED -22
#define OTA_WITH_SEQUENTIAL_WRITES 0xfffffffe
#define HTTP_TRANSPORT_OVER_SSL 1
typedef struct {
  bool (*cancelled)(void *);
  bool (*begin_commit)(void *);
  void *context;
} ota_control_t;
typedef void (*ota_progress_callback_t)(const char *);
typedef struct {
  const char *url;
  void *crt_bundle_attach;
  int timeout_ms, buffer_size, buffer_size_tx;
  bool disable_auto_redirect;
} esp_http_client_config_t;
typedef void *esp_http_client_handle_t;
typedef int esp_ota_handle_t;
typedef struct {
  size_t size;
} esp_partition_t;
static esp_partition_t partition = {16384};
static void *esp_crt_bundle_attach;
static bool cancelled, commit_allowed, complete, downgrade;
static int cancel_at, read_calls, aborts, ends, boots, cleanups, redirects, http_status, read_error, write_error,
    end_error;
static size_t expected, remaining, written;
static void reset(void) {
  cancelled = complete = downgrade = false;
  commit_allowed = true;
  cancel_at = read_calls = aborts = ends = boots = cleanups = redirects = read_error = write_error = end_error = 0;
  http_status = 200;
  expected = remaining = 8192;
  written = 0;
}
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg) {
  assert(cfg->timeout_ms == 5000 && cfg->crt_bundle_attach == esp_crt_bundle_attach);
  return (void *)1;
}
static int esp_http_client_open(void *client, int size) {
  (void)client;
  (void)size;
  return ESP_OK;
}
static int64_t esp_http_client_fetch_headers(void *client) {
  (void)client;
  return expected;
}
static int esp_http_client_get_status_code(void *client) {
  (void)client;
  return http_status;
}
static int esp_http_client_set_redirection(void *client) {
  (void)client;
  ++redirects;
  http_status = 200;
  return ESP_OK;
}
static int esp_http_client_get_transport_type(void *client) {
  (void)client;
  return downgrade ? 0 : HTTP_TRANSPORT_OVER_SSL;
}
static int esp_http_client_close(void *client) {
  (void)client;
  return ESP_OK;
}
static int esp_http_client_cleanup(void *client) {
  assert(client);
  ++cleanups;
  return ESP_OK;
}
static const esp_partition_t *esp_ota_get_next_update_partition(void *unused) {
  (void)unused;
  return &partition;
}
static int esp_ota_begin(const esp_partition_t *slot, unsigned size, int *handle) {
  assert(slot == &partition && size == OTA_WITH_SEQUENTIAL_WRITES);
  *handle = 1;
  return ESP_OK;
}
static bool esp_http_client_is_complete_data_received(void *client) {
  (void)client;
  return complete;
}
static int esp_http_client_read(void *client, char *buffer, int length) {
  (void)client;
  (void)buffer;
  ++read_calls;
  if (cancel_at && read_calls == cancel_at)
    cancelled = true;
  if (read_error)
    return read_error;
  if (!remaining) {
    complete = true;
    return 0;
  }
  size_t count = remaining < (size_t)length ? remaining : (size_t)length;
  remaining -= count;
  return (int)count;
}
static int esp_ota_write(int handle, const void *buffer, size_t length) {
  assert(handle && buffer);
  if (write_error)
    return write_error;
  written += length;
  return ESP_OK;
}
static int esp_ota_abort(int handle) {
  assert(handle);
  ++aborts;
  return ESP_OK;
}
static int esp_ota_end(int handle) {
  assert(handle && !cancelled);
  ++ends;
  return end_error;
}
static int esp_ota_set_boot_partition(const esp_partition_t *slot) {
  assert(slot == &partition && ends == 1 && !end_error && !cancelled);
  ++boots;
  return ESP_OK;
}
static bool is_cancelled(void *unused) {
  (void)unused;
  return cancelled;
}
static bool begin_commit(void *unused) {
  (void)unused;
  return commit_allowed;
}
#include "ota_functions.inc"

int main(void) {
  host_test_init();
  const char *url = "https://github.com/techfoundrynz/Pubmote/releases/download/v1.0.0/test.bin";
  const ota_control_t control = {is_cancelled, begin_commit, NULL};
  reset();
  assert(apply_ota(url, NULL, &control) == ESP_OK);
  assert(boots == 1 && ends == 1 && aborts == 0 && cleanups == 1 && written == 8192);
  for (int stage = 1; stage <= 3; ++stage) {
    reset();
    cancel_at = stage;
    assert(apply_ota(url, NULL, &control) == ESP_ERR_INVALID_STATE);
    assert(!boots && !ends && aborts == 1 && cleanups == 1);
  }
  reset();
  cancelled = true;
  assert(apply_ota(url, NULL, &control) == ESP_ERR_INVALID_STATE && !cleanups && !aborts && !boots);
  reset();
  commit_allowed = false;
  assert(apply_ota(url, NULL, &control) == ESP_ERR_INVALID_STATE && aborts == 1 && !boots && !ends);
  reset();
  read_error = -ESP_ERR_HTTP_EAGAIN;
  assert(apply_ota(url, NULL, &control) == ESP_ERR_TIMEOUT && aborts == 1 && !boots && cleanups == 1);
  reset();
  remaining = 1024;
  assert(apply_ota(url, NULL, &control) == ESP_ERR_INVALID_RESPONSE && aborts == 1 && !boots);
  reset();
  end_error = ESP_ERR_OTA_VALIDATE_FAILED;
  assert(apply_ota(url, NULL, &control) == ESP_ERR_OTA_VALIDATE_FAILED && !boots && !aborts && ends == 1);
  reset();
  write_error = ESP_FAIL;
  assert(apply_ota(url, NULL, &control) == ESP_FAIL && aborts == 1 && !boots);
  reset();
  http_status = 302;
  assert(apply_ota(url, NULL, &control) == ESP_OK && redirects == 1 && boots == 1);
  reset();
  http_status = 302;
  downgrade = true;
  assert(apply_ota(url, NULL, &control) == ESP_ERR_INVALID_RESPONSE && !boots && !aborts && cleanups == 1);
  reset();
  http_status = 404;
  assert(apply_ota(url, NULL, &control) == ESP_ERR_INVALID_RESPONSE && !boots);
  reset();
  expected = partition.size + 1;
  assert(apply_ota(url, NULL, &control) == ESP_ERR_INVALID_SIZE && !boots && !aborts);
  reset();
  expected = 0; // Chunked/unknown length.
  assert(apply_ota(url, NULL, &control) == ESP_OK && written == 8192 && boots == 1);
  reset();
  complete = true; // A whole response can already be prefetched by header parsing.
  assert(apply_ota(url, NULL, &control) == ESP_OK && written == 8192);
  puts("OTA cancellation, commit arbitration, redirects, integrity failures and cleanup passed");
}
