#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "mqtt_client.h"
#include "dht.h"

#define DHT_PIN 23
#define WIFI_SSID "@felipehoffmeister_" // Substitua pelo seu SSID
#define WIFI_PASS "hoff1234"            // Substitua pela sua senha
#define MQTT_TOPIC_TEMP "graduacao/iot/6/temperatura" // Temperatura
#define MQTT_TOPIC_HUMID "graduacao/iot/6/umidade" // Umidade

static const char *TAG = "MQTT_DHT";
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

static esp_mqtt_client_handle_t client;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT conectado ao broker!");
            // Enviar uma mensagem de teste assim que conectado
           // esp_mqtt_client_publish(client, MQTT_TOPIC_TEMP, "test", 0, 1, 0);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT desconectado do broker!");
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Erro no MQTT!");
            break;
        default:
            break;
    }
}

// Função de manipulação de eventos WiFi
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    }
}

// Inicialização do Wi-Fi
void wifi_init() {
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// Inicialização do MQTT
void mqtt_app_start() {
    esp_mqtt_client_config_t mqtt_config = {
       .broker.address.uri = "mqtt://test.mosquitto.org",  
    };

    client = esp_mqtt_client_init(&mqtt_config);  // Inicializa o cliente MQTT com a configuração
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);  // Inicia o cliente MQTT
    esp_mqtt_client_subscribe(client, "graduacao/iot/6/temperatura", 0);  // Inscreve no tópico de temperatura
    esp_mqtt_client_subscribe(client, "graduacao/iot/6/umidade", 0);// Inscreve no tópico de umidade
}

// Função de leitura do sensor DHT

void publish_temperature_and_humidity(float temperature, float humidity) {
    char temp_payload[50];  
    char hum_payload[50];   
    
    
    snprintf(temp_payload, sizeof(temp_payload), "Temperatura: %.1f°C", temperature);
    snprintf(hum_payload, sizeof(hum_payload), "Umidade: %.1f%%", humidity);
    
    // Publicar as mensagens nos tópicos MQTT
    esp_mqtt_client_publish(client, MQTT_TOPIC_TEMP, temp_payload, 0, 1, 0);  // Publicando temperatura
    esp_mqtt_client_publish(client, MQTT_TOPIC_HUMID, hum_payload, 0, 1, 0);   // Publicando umidade
}
void sensor_task(void *pvParameters) {
    int16_t temperature = 0, humidity = 0;

    while (true) {
        esp_err_t result = dht_read_data(DHT_TYPE_DHT11, DHT_PIN, &humidity, &temperature);

        if (result == ESP_OK) {
            float temperature_f = temperature / 10.0;
            float humidity_f = humidity / 10.0;

            ESP_LOGI(TAG, "Temperatura: %.1f°C, Umidade: %.1f%%", temperature_f, humidity_f);

            publish_temperature_and_humidity(temperature_f,humidity_f);
        } else {
            ESP_LOGE(TAG, "Erro na leitura do sensor: %s", esp_err_to_name(result));
        }

        vTaskDelay(pdMS_TO_TICKS(15000)); // Leitura a cada 15 segundos
    }
}

// Função principal
void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    mqtt_app_start();

    // Criação da task de leitura do sensor
    xTaskCreate(sensor_task, "Sensor Task", 4096, NULL, 1, NULL);
}