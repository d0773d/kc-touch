#include "yamui_loader_fs.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_littlefs.h"

static const char *TAG = "yamui_fs";

static bool s_mounted = false;

esp_err_t yamui_fs_init(const char *partition_label, const char *mount_point)
{
    if (s_mounted) {
        return ESP_OK;
    }
    if (!partition_label || !mount_point) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_vfs_littlefs_conf_t conf = {
        .base_path = mount_point,
        .partition_label = partition_label,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format LittleFS partition '%s'",
                     partition_label);
        } else {
            ESP_LOGE(TAG, "LittleFS init failed (%s): %s",
                     partition_label, esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS mounted at %s — total: %u, used: %u",
                 mount_point, (unsigned)total, (unsigned)used);
    }

    s_mounted = true;
    return ESP_OK;
}

void yamui_fs_deinit(const char *partition_label)
{
    if (!s_mounted || !partition_label) {
        return;
    }
    esp_vfs_littlefs_unregister(partition_label);
    s_mounted = false;
}

bool yamui_fs_exists(const char *path)
{
    if (!path) {
        return false;
    }
    struct stat st;
    return stat(path, &st) == 0;
}

esp_err_t yamui_fs_save(const char *path, const char *data, size_t length)
{
    if (!path || !data || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing: %s", path, strerror(errno));
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, length, f);
    fclose(f);

    if (written != length) {
        ESP_LOGE(TAG, "Short write to %s: %u/%u bytes",
                 path, (unsigned)written, (unsigned)length);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved %u bytes to %s", (unsigned)length, path);
    return ESP_OK;
}

esp_err_t yamui_fs_load(const char *path, char **out_data, size_t *out_length)
{
    if (!path || !out_data || !out_length) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }
    long size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return ESP_FAIL;
    }

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t read_bytes = fread(buf, 1, (size_t)size, f);
    fclose(f);

    if (read_bytes != (size_t)size) {
        free(buf);
        return ESP_FAIL;
    }

    buf[size] = '\0';
    *out_data = buf;
    *out_length = (size_t)size;

    ESP_LOGI(TAG, "Loaded %u bytes from %s", (unsigned)size, path);
    return ESP_OK;
}

esp_err_t yamui_fs_delete(const char *path)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    if (remove(path) != 0) {
        ESP_LOGW(TAG, "Failed to delete %s: %s", path, strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Deleted %s", path);
    return ESP_OK;
}
