/* Host-only Console lifecycle stubs for the UI simulator. */
#include "tab5_app_boot.h"

#include <stddef.h>

esp_err_t tab5_app_boot_check_layout(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool tab5_app_boot_console_available(void)
{
    return false;
}

esp_err_t tab5_app_boot_prepare_launch(const esp_partition_t **slot)
{
    if (slot) *slot = NULL;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t tab5_app_boot_select(const esp_partition_t *slot)
{
    (void)slot;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t tab5_app_boot_ready(bool hardware_ok)
{
    (void)hardware_ok;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t tab5_app_boot_return_to_console(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}
