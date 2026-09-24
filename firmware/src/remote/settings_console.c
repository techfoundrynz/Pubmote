#include "settings_console.h"
#include "settings_api.h"
#include "settings_types.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>

// Replies echo the request id so a late reply is never taken for a newer request.
static bool valid_id(const char *id) {
  size_t length = 0;
  for (; id[length]; ++length) {
    unsigned char c = (unsigned char)id[length];
    if (length == 32 || !(isalnum(c) || c == '_' || c == '-')) {
      return false;
    }
  }
  return length > 0;
}

static int reply_error(const char *message, const char *id) {
  cJSON *reply = cJSON_CreateObject();
  char *text = NULL;
  if (reply && cJSON_AddStringToObject(reply, "kind", "settings_result") &&
      cJSON_AddNumberToObject(reply, "version", SETTINGS_VERSION) && cJSON_AddBoolToObject(reply, "ok", false) &&
      cJSON_AddStringToObject(reply, "error", message) && (!id || cJSON_AddStringToObject(reply, "id", id))) {
    text = cJSON_PrintUnformatted(reply);
  }
  if (text) {
    puts(text);
    cJSON_free(text);
  }
  else {
    // A static frame still reports failure when the JSON allocator is exhausted.
    printf("{\"kind\":\"settings_result\",\"version\":%d,\"ok\":false,\"error\":\"Out of memory\"%s%s%s}\n",
           SETTINGS_VERSION, id ? ",\"id\":\"" : "", id ? id : "", id ? "\"" : "");
  }
  cJSON_Delete(reply);
  return -1;
}

int console_get_settings(int argc, char **argv) {
  const char *id = argc == 2 ? argv[1] : NULL;
  if (argc < 1 || argc > 2) {
    return reply_error("Usage: settings [id]", NULL);
  }
  if (id && !valid_id(id)) {
    return reply_error("Invalid request id", NULL);
  }
  cJSON *reply = settings_describe_json();
  char *text = reply && (!id || cJSON_AddStringToObject(reply, "id", id)) ? cJSON_PrintUnformatted(reply) : NULL;
  cJSON_Delete(reply);
  if (!text) {
    return reply_error("Unable to allocate settings response", id);
  }
  puts(text);
  cJSON_free(text);
  return 0;
}

int console_save_settings(int argc, char **argv) {
  const char *id = argc == 3 ? argv[2] : NULL;
  if (argc < 2 || argc > 3) {
    return reply_error("Expected one quoted JSON object and an optional id", NULL);
  }
  if (id && !valid_id(id)) {
    return reply_error("Invalid request id", NULL);
  }
  char error[128];
  if (settings_apply_json(argv[1], error, sizeof(error)) != 0) {
    return reply_error(error, id);
  }
  printf("{\"kind\":\"settings_result\",\"version\":%d,\"ok\":true%s%s%s}\n", SETTINGS_VERSION, id ? ",\"id\":\"" : "",
         id ? id : "", id ? "\"" : "");
  return 0;
}
