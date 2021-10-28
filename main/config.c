#include "esp-mgba.h"
#include "mobile.h"

#include <esp_spiffs.h>

static const char *filename = "/spiffs/cfg.bin";
static FILE *ConfigFile;

bool mobile_board_config_read(void *user, void *dest, const uintptr_t offset, const size_t size)
{
    fseek(ConfigFile, offset, SEEK_SET);
    return fread(dest, 1, size, ConfigFile) == size;
}

bool mobile_board_config_write(void *user, const void *src, const uintptr_t offset, const size_t size)
{
    fseek(ConfigFile, offset, SEEK_SET);
    return fwrite(src, 1, size, ConfigFile) == size;
}

void init_config(void)
{
    static const esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = "spiffs",
      .max_files = 5,
      .format_if_mount_failed = true
    };

    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));

    ConfigFile = fopen(filename, "r+");
    if (ConfigFile == NULL)
      ConfigFile = fopen(filename, "w+");
}
