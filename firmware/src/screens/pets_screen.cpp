#include "screens/pets_screen.h"
#include "cJSON.h"
#include "config.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/queue.h"
#include "remote/connection.h"
#include "remote/display.h"
#include "remote/receiver.h"
#include "remote/settings.h"
#include "remote/transmitter.h"
#include "remote/wifi.h"
#include "slint_generated/app-window.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
constexpr size_t PIXELS = 384 * 468;
constexpr size_t PACKAGE_SIZE = 16 + PIXELS * 4;
constexpr int PAGE_SIZE = 1;
// Must fit the ~9 KiB contiguous internal block available after radio handoff.
// Keep this stack internal: Apply writes flash, which can disable the PSRAM cache.
constexpr uint32_t WORKER_STACK_BYTES = 8192;
constexpr char TAG[] = "PETS";
constexpr char API[] = API_BASE_URL "/pets/";
struct Selection {
  uint32_t version = 1;
  uint32_t slot = 0;
  uint32_t enabled = 0;
  char name[96] = {};
};
Selection selected;
struct Entry {
  std::string id;
  std::string name;
};
std::array<Entry, PAGE_SIZE> entries;
std::shared_ptr<slint::VectorModel<PetCard>> cards;
std::shared_ptr<uint8_t> preview_package;
QueueHandle_t jobs = nullptr;
TaskHandle_t worker = nullptr;
int entry_count = 0;
bool mounted = false;
bool online = false; // UI thread only; leaving download mode requires restart, like OTA.
bool radio_ready = false;
bool busy = false;
bool applying = false; // UI thread only.
std::atomic<int> requested_page{0};
std::atomic<uint32_t> request_generation{0};
std::string path(unsigned slot) {
  return "/pets/slot" + std::to_string(slot) + ".pet";
}
const UiState &ui() {
  return get_slint_window()->global<UiState>();
}

void status(const char *message) {
  ui().set_pets_status(message);
}
void post_status(const char *message, uint32_t generation) {
  slint::SharedString text(message);
  slint::invoke_from_event_loop([text, generation]() {
    if (request_generation.load() == generation)
      ui().set_pets_status(text);
  });
}

bool mount_storage(bool initialize) {
  if (mounted)
    return true;
  esp_vfs_littlefs_conf_t config = {};
  config.base_path = "/pets";
  config.partition_label = "littlefs";
  config.dont_mount = false;
  if (esp_vfs_littlefs_register(&config) == ESP_OK)
    return mounted = true;
  if (!initialize)
    return false;
  // Only initialize a completely erased partition. Never auto-format an existing filesystem.
  const auto *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "littlefs");
  if (!partition)
    return false;
  uint8_t block[256];
  for (size_t offset = 0; offset < partition->size; offset += sizeof(block)) {
    if (esp_partition_read(partition, offset, block, sizeof(block)) != ESP_OK)
      return false;
    for (uint8_t value : block)
      if (value != 0xff)
        return false;
  }
  config.format_if_mount_failed = true;
  return mounted = esp_vfs_littlefs_register(&config) == ESP_OK;
}

bool valid_header(const uint8_t *h) {
  const uint8_t expected[16] = {'P', 'M', 'P', 'E', 'T', '0', '1', 0, 48, 0, 52, 0, 9, 0, 4, 1};
  return std::memcmp(h, expected, sizeof(expected)) == 0;
}

slint::Image load_image(unsigned slot) {
  FILE *file = std::fopen(path(slot).c_str(), "rb");
  if (!file)
    return {};
  uint8_t header[16];
  bool valid = std::fread(header, 1, 16, file) == 16 && valid_header(header);
  valid = valid && heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= PIXELS * 4 + 32768;
  if (!valid) {
    std::fclose(file);
    return {};
  }
  slint::SharedPixelBuffer<slint::Rgba8Pixel> pixels(384, 468);
  valid =
      std::fread(pixels.begin(), 1, PIXELS * 4, file) == PIXELS * 4 && std::fgetc(file) == EOF && !std::ferror(file);
  std::fclose(file);
  return valid ? slint::Image(pixels) : slint::Image();
}

bool save_selection(const Selection &next) {
  Selection value = next;
  if (nvs_write_blob("pet_selection", &value, sizeof(value)) != ESP_OK) {
    status("Could not save pet settings.");
    return false;
  }
  selected = value;
  return true;
}

// Downloads stream into a bounded destination. HTTP errors and incomplete bodies never commit.
bool get(const std::string &url, FILE *file, std::string *json, size_t maximum, uint8_t *raw, uint32_t generation) {
  if (request_generation.load() != generation)
    return false;
  ESP_LOGI(TAG, "GET %s", url.c_str());
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  if (url.compare(0, 8, "https://") == 0)
    config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 15000;
  config.disable_auto_redirect = true;
  auto client = esp_http_client_init(&config);
  if (!client)
    return false;
  bool ok = esp_http_client_open(client, 0) == ESP_OK;
  if (ok) {
    const int64_t length = esp_http_client_fetch_headers(client);
    ok = length >= 0 && esp_http_client_get_status_code(client) == 200 && (uint64_t)length <= maximum;
  }
  char buffer[1024];
  size_t total = 0;
  while (ok) {
    if (request_generation.load() != generation) {
      ok = false;
      break;
    }
    int count = esp_http_client_read(client, buffer, sizeof(buffer));
    if (count < 0) {
      ok = false;
      break;
    }
    if (count == 0)
      break;
    total += count;
    if (total > maximum) {
      ok = false;
      break;
    }
    if (file && std::fwrite(buffer, 1, count, file) != (size_t)count) {
      ok = false;
      break;
    }
    if (json)
      json->append(buffer, count);
    if (raw)
      std::memcpy(raw + total - count, buffer, count);
  }
  ok = ok && esp_http_client_is_complete_data_received(client) && (!file || total == PACKAGE_SIZE) &&
       (!raw || total == maximum);
  ESP_LOGI(TAG, "HTTP %d: %u bytes, %s", esp_http_client_get_status_code(client), (unsigned)total,
           ok ? "complete" : "failed");
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return ok;
}

struct Job {
  int page = 0;
  Entry pet;
  unsigned slot = 0;
  std::shared_ptr<uint8_t> package;
  uint32_t generation = 0;
};
void start(Job *job);

void execute_job(Job *job) {
  bool ok = true;
  Entry result;
  int pages = 0;
  std::shared_ptr<uint8_t> package;
  std::string error;
  if (job->page) {
    post_status("Connecting to Wi-Fi...", job->generation);
    esp_err_t wifi_error = request_generation.load() != job->generation ? ESP_ERR_INVALID_STATE
                           : wifi_is_initialized()                      ? ESP_OK
                                                                        : wifi_init();
    const bool init_failed = wifi_error != ESP_OK;
    if (wifi_error == ESP_OK && wifi_get_connection_state() != WIFI_STATE_CONNECTED)
      wifi_error = wifi_connect_to_network(get_wifi_ssid(), get_wifi_password());
    ok = wifi_error == ESP_OK;
    if (!ok) {
      error = std::string(init_failed ? "Wi-Fi startup: " : "Wi-Fi connection: ") + esp_err_to_name(wifi_error);
      ESP_LOGE(TAG, "%s; internal heap %u, largest %u", error.c_str(),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    }
    if (ok) {
      post_status("Loading pet...", job->generation);
      std::string json;
      ok = get(std::string(API) + "catalog?page=" + std::to_string(job->page) + "&pageSize=1", nullptr, &json, 8192,
               nullptr, job->generation);
      error = "Catalog unavailable. Tap Retry.";
      cJSON *root = ok ? cJSON_Parse(json.c_str()) : nullptr;
      auto *pets = cJSON_GetObjectItemCaseSensitive(root, "pets");
      auto *total = cJSON_GetObjectItemCaseSensitive(root, "pages");
      ok = cJSON_IsArray(pets) && cJSON_GetArraySize(pets) == 1 && cJSON_IsNumber(total) && total->valuedouble >= 1 &&
           total->valuedouble <= 10000;
      if (ok) {
        pages = total->valueint;
        auto *pet = cJSON_GetArrayItem(pets, 0);
        auto *id = cJSON_GetObjectItemCaseSensitive(pet, "id");
        auto *name = cJSON_GetObjectItemCaseSensitive(pet, "name");
        ok = cJSON_IsString(id) && cJSON_IsString(name) && id->valuestring[0] && std::strlen(id->valuestring) <= 80 &&
             std::strspn(id->valuestring, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") ==
                 std::strlen(id->valuestring);
        if (ok)
          result = {id->valuestring, name->valuestring};
      }
      cJSON_Delete(root);
    }
    if (ok && request_generation.load() == job->generation) {
      post_status("Loading animations...", job->generation);
      package =
          std::shared_ptr<uint8_t>(static_cast<uint8_t *>(heap_caps_malloc(PACKAGE_SIZE, MALLOC_CAP_8BIT)), std::free);
      error = "Not enough memory for preview.";
      ok = bool(package);
      if (ok) {
        ok = get(std::string(API) + result.id + ".pet", nullptr, nullptr, PACKAGE_SIZE, package.get(),
                 job->generation) &&
             valid_header(package.get());
        error = "Preview unavailable. Tap Retry.";
      }
    }
  }
  else {
    // Applying the already-previewed bytes works even if Wi-Fi has disconnected.
    ok = job->package && valid_header(job->package.get()) && mount_storage(true);
    error = "Pet storage unavailable or full.";
    if (ok) {
      std::remove(path(job->slot).c_str());
      std::remove("/pets/download.tmp");
      FILE *file = std::fopen("/pets/download.tmp", "wb");
      ok = file && std::fwrite(job->package.get(), 1, PACKAGE_SIZE, file) == PACKAGE_SIZE;
      if (file && std::fclose(file) != 0)
        ok = false;
      if (ok)
        ok = std::rename("/pets/download.tmp", path(job->slot).c_str()) == 0;
      if (!ok)
        std::remove("/pets/download.tmp");
      error = "Could not save pet. Installed pet unchanged.";
    }
  }
  Job completed = *job;
  delete job;
  slint::invoke_from_event_loop([ok, error, completed, result, pages, package]() mutable {
    if (completed.page && completed.generation != request_generation.load()) {
      // Ignore superseded results/errors, release their atlas, and load only
      // the latest destination. Never queue one download per button press.
      package.reset();
      start(new Job{requested_page.load(), {}, 0, {}});
      return;
    }
    busy = false;
    applying = false;
    ui().set_pets_busy(false);
    if (!ok) {
      status(error.c_str());
      return;
    }
    if (completed.page) {
      if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < PIXELS * 4 + 32768) {
        status("Not enough memory for preview.");
        return;
      }
      slint::SharedPixelBuffer<slint::Rgba8Pixel> pixels(384, 468);
      std::memcpy(pixels.begin(), package.get() + 16, PIXELS * 4);
      PetCard card{};
      card.name = result.name.c_str();
      card.atlas = slint::Image(pixels);
      cards = std::make_shared<slint::VectorModel<PetCard>>();
      cards->push_back(card);
      entries[0] = result;
      entry_count = 1;
      preview_package = package;
      ui().set_pet_cards(cards);
      ui().set_pet_selected_index(0);
      ui().set_pet_can_apply(true);
      ui().set_pets_page(completed.page);
      ui().set_pets_pages(pages);
      status("");
    }
    else {
      // Reuse the preview image; avoid allocating a second full atlas on Apply.
      Selection next;
      next.slot = completed.slot;
      next.enabled = 1;
      std::snprintf(next.name, sizeof(next.name), "%s", completed.pet.name.c_str());
      if (!save_selection(next))
        return;
      ui().set_pet_atlas(cards->row_data(0)->atlas);
      ui().set_pet_enabled(true);
      ui().set_pet_installed_name(selected.name);
      ui().set_pet_can_apply(false);
      status("Applied. Tap Exit to return to the main screen.");
    }
  });
}

void download_task(void *) {
  for (;;) {
    Job *job = nullptr;
    if (xQueueReceive(jobs, &job, portMAX_DELAY) == pdTRUE) {
      // Coalesce quick taps before starting a new HTTP request.
      if (job->page) {
        do {
          job->generation = request_generation.load();
          job->page = requested_page.load();
          vTaskDelay(pdMS_TO_TICKS(150));
        } while (job->generation != request_generation.load());
      }
      execute_job(job);
      ESP_LOGI(TAG, "Worker minimum free stack: %u bytes", (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    }
  }
}

void start(Job *job) {
  busy = true;
  applying = job->page == 0;
  if (job->page) {
    preview_package.reset();
    cards.reset();
    ui().set_pet_cards(std::make_shared<slint::VectorModel<PetCard>>());
    ui().set_pet_can_apply(false);
    ui().set_pet_selected_index(-1);
    entry_count = 0;
  }
  ui().set_pets_busy(true);
  status(job->page ? "Loading gallery..." : "Applying pet...");
  // Keep the stack allocated across requests: Wi-Fi consumes internal SRAM after startup.
  if (!jobs)
    jobs = xQueueCreate(1, sizeof(Job *));
  if (jobs && !worker && xTaskCreate(download_task, "pets_download", WORKER_STACK_BYTES, nullptr, 5, &worker) != pdPASS)
    worker = nullptr;
  if (!worker || xQueueSend(jobs, &job, 0) != pdTRUE) {
    ESP_LOGE(TAG, "Cannot queue request; internal heap %u, largest %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    delete job;
    busy = false;
    applying = false;
    ui().set_pets_busy(false);
    status("Not enough memory to start download task. Restart and retry.");
  }
}
} // namespace

void pets_init() {
  Selection stored;
  if (nvs_read_blob("pet_selection", &stored, sizeof(stored)) != ESP_OK || stored.version != 1 || stored.slot > 1 ||
      stored.enabled > 1)
    return;
  stored.name[sizeof(stored.name) - 1] = 0;
  if (!mount_storage(false))
    return;
  auto image = load_image(stored.slot);
  if (!image.size().width)
    return;
  selected = stored;
  ui().set_pet_atlas(image);
  ui().set_pet_enabled(selected.enabled);
  ui().set_pet_installed_name(selected.name);
}

void setup_pets_properties() {
  if (!online)
    status("Browse codex-pets.net or enable your installed pet.");
}
void handle_open_pets() {
  ui().set_screen(Screen::Pets);
}
void handle_pets_browse(int page) {
  if (applying || page < 1 || page > 10000)
    return;
  if (online) {
    if (!radio_ready) {
      status("Wi-Fi setup failed. Exit and retry.");
      return;
    }
    const int pages = ui().get_pets_pages();
    if (pages > 0 && page > pages)
      return;
    requested_page.store(page);
    request_generation.fetch_add(1);
    ui().set_pets_page(page); // Count every tap immediately, even during loading.
    if (!busy)
      start(new Job{page, {}, 0, {}});
    return;
  }
  const char *ssid = get_wifi_ssid();
  if (!ssid || !ssid[0]) {
    status("Configure Wi-Fi at pubmote.com first.");
    return;
  }
  ui().set_confirm_dialog_title("Browse pets?");
  ui().set_confirm_dialog_message("Browsing disconnects the board. Exit restarts the remote to reconnect.");
  ui().set_confirm_dialog_confirm_text("Browse");
  ui().on_confirm_dialog_rejected([]() { ui().set_show_confirm_dialog(false); });
  ui().on_confirm_dialog_accepted([page]() {
    ui().set_show_confirm_dialog(false);
    online = true;
    ui().set_pets_online(true);
    requested_page.store(page);
    request_generation.fetch_add(1);
    status("Preparing Wi-Fi...");
    receiver_deinit();
    transmitter_deinit();
    connection_update_state(CONNECTION_STATE_DISCONNECTED);
    const esp_err_t result = comms_is_initialized() ? comms_prepare_wifi() : ESP_OK;
    if (result != ESP_OK) {
      status("Wi-Fi setup failed. Exit and retry.");
      return;
    }
    radio_ready = true;
    start(new Job{page, {}, 0, {}});
  });
  ui().set_show_confirm_dialog(true);
}
void handle_pet_download(int index) {
  if (busy || !online || !cards || index < 0 || index >= entry_count)
    return;
  ESP_LOGI(TAG, "Select gallery pet %d", index);
  ui().set_pet_selected_index(index);
  ui().set_pet_can_apply(cards->row_data(index)->atlas.size().width > 0);
}
void handle_pet_apply() {
  const int index = ui().get_pet_selected_index();
  if (busy || !online || !ui().get_pet_can_apply() || index < 0 || index >= entry_count)
    return;
  start(new Job{0, entries[index], 1 - selected.slot, preview_package});
}
void handle_pet_toggle() {
  if (busy || !ui().get_pet_atlas().size().width)
    return;
  Selection next = selected;
  next.enabled = !next.enabled;
  if (save_selection(next))
    ui().set_pet_enabled(selected.enabled);
}
void handle_pets_back() {
  if (busy)
    return;
  if (online)
    esp_restart();
  ui().set_screen(Screen::Menu);
}

bool pets_requires_restart() {
  return online;
}
