#pragma once
#include <esp_openthread_types.h>

// Native C6 platform defaults from Espressif's public-domain light example:
// examples/light/main/app_priv.h at ae9001236dddd3f5fd953bed1c4f483c1b9beba3.
// These example-local macros are not supplied by the IDF driver headers.
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG() { .radio_mode = RADIO_MODE_NATIVE }
#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG() { .host_connection_mode = HOST_CONNECTION_MODE_NONE }
#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG() \
    { .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10 }
