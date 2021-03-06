#include <string.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs_flash.h"

#include "hap.h"

#define TAG "Touch Switch"

#define ACCESSORY_NAME  "Touch Switch"
#define MANUFACTURER_NAME   "wumbo"
#define MODEL_NAME  "ESP32_ACC"
#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

#define EXAMPLE_ESP_WIFI_SSID "SSID"
#define EXAMPLE_ESP_WIFI_PASS "PASSWORD"

#define TOUCH_PAD_NO_CHANGE   (-1)
#define TOUCH_THRESH_NO_USE   (0)
#define TOUCH_PAD_THRESH      (250)

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static void* a;
static void* _ev_handle;
static int state = false;

void* switch_read(void* arg)
{
    printf("[MAIN] STATE READ\n");
    
    return (void*)state;
}

void switch_write(void* arg, void* value, int len)
{
    printf("[MAIN] STATE WRITE. %d\n", (int)value);

    state = (int)value;
    if (value) {
        state = true;
    }
    else {
        state = false;
    }

    if (_ev_handle)
        hap_event_response(a, _ev_handle, (void*)state);

    return;
}

void switch_notify(void* arg, void* ev_handle, bool enable)
{
    if (enable) {
        _ev_handle = ev_handle;
    }
    else {
        _ev_handle = NULL;
    }
}

static bool _identifed = false;
void* identify_read(void* arg)
{
    return (void*)true;
}

void hap_object_init(void* arg)
{
    void* accessory_object = hap_accessory_add(a);
    struct hap_characteristic cs[] = {
        {HAP_CHARACTER_IDENTIFY, (void*)true, NULL, identify_read, NULL, NULL},
        {HAP_CHARACTER_MANUFACTURER, (void*)MANUFACTURER_NAME, NULL, NULL, NULL, NULL},
        {HAP_CHARACTER_MODEL, (void*)MODEL_NAME, NULL, NULL, NULL, NULL},
        {HAP_CHARACTER_NAME, (void*)ACCESSORY_NAME, NULL, NULL, NULL, NULL},
        {HAP_CHARACTER_SERIAL_NUMBER, (void*)"0123456789", NULL, NULL, NULL, NULL},
        {HAP_CHARACTER_FIRMWARE_REVISION, (void*)"1.0", NULL, NULL, NULL, NULL},
    };
    hap_service_and_characteristics_add(a, accessory_object, HAP_SERVICE_ACCESSORY_INFORMATION, cs, ARRAY_SIZE(cs));

    struct hap_characteristic cc[] = {
        {HAP_CHARACTER_ON, (void*)state, NULL, switch_read, switch_write, switch_notify},
    };
    hap_service_and_characteristics_add(a, accessory_object, HAP_SERVICE_SWITCHS, cc, ARRAY_SIZE(cc));
}


static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "got ip:%s",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        {
            hap_init();

            uint8_t mac[6];
            esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
            char accessory_id[32] = {0,};
            sprintf(accessory_id, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            hap_accessory_callback_t callback;
            callback.hap_object_init = hap_object_init;
            a = hap_accessory_register((char*)ACCESSORY_NAME, accessory_id, (char*)"053-58-197", (char*)MANUFACTURER_NAME, HAP_ACCESSORY_CATEGORY_OTHER, 811, 1, NULL, &callback);
        }
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

void wifi_init_sta()
{
    wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}

static void update_state_task(void * pvParameter) {
    while (1) {
        uint16_t touch_val;
        touch_pad_read(TOUCH_PAD_NUM0, &touch_val);
        printf("TOUCH VALUE: %d\n", touch_val);

        if (touch_val < TOUCH_PAD_THRESH) {
            switch_write(NULL, !state, 1);
            vTaskDelay(25);
        } else {
            vTaskDelay(5);
        }
    }
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());

    touch_pad_init();
    touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
    touch_pad_config(TOUCH_PAD_NUM0, TOUCH_THRESH_NO_USE);
    uint16_t touch_val = 0;
    touch_pad_read(TOUCH_PAD_NUM0, &touch_val);
    sleep(1);
    
    xTaskCreate(update_state_task, "Task1", 2048, NULL, tskIDLE_PRIORITY, NULL);

    wifi_init_sta();
}
