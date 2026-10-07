#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/ledc.h"
#include "mqtt_client.h"

// ------------------- [사용자 환경 설정] -------------------
#define WIFI_SSID           CONFIG_WIFI_SSID        // 집 공유기 SSID
#define WIFI_PASS           CONFIG_WIFI_PASSWORD    // 집 공유기 비밀번호
#define MQTT_BROKER_URI     CONFIG_MQTT_BROKER_URI // 우분투 서버 내부 IP

#define TOPIC_FEED_CMD      "home/feeder/command"
#define TOPIC_FEED_STATUS   "home/feeder/status"
#define FEED_DURATION_MS     2000  // 사료 배급 시간 (밀리초 단위)
// ------------------- [LED 설정] -------------------
#define LED_PIN             19
#define LED_ON              0
#define LED_OFF             1
// ------------------- [MG90S 서보 파라미터] -------------------
#define SERVO_GPIO          18
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_14_BIT
#define LEDC_FREQUENCY      50

#define SERVO_STOP_DUTY     1229  // ~1.5ms 정지
#define SERVO_CW_SLOW       1065  // ~1.3ms 시계방향 저속 토출

static const char *TAG = "FEEDER_MQTT";
static esp_mqtt_client_handle_t mqtt_client = NULL;

// 1. 하드웨어 PWM(LEDC) 초기화
static void servo_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t ch_conf = {
        .gpio_num       = SERVO_GPIO,
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = LEDC_TIMER,
        .duty           = SERVO_STOP_DUTY,
        .hpoint         = 0
    };
    ledc_channel_config(&ch_conf);
}

// 2. 사료 배급 동작 (블로킹 방지를 위해 태스크 분리 가능)
void feed_dispense(uint32_t duration_ms)
{
    ESP_LOGI(TAG, "사료 배급 시작...");
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, SERVO_CW_SLOW);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    gpio_set_level(LED_PIN, LED_ON);  // LED 켜기
    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, SERVO_STOP_DUTY);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    gpio_set_level(LED_PIN, LED_OFF);  // LED 끄기
    ESP_LOGI(TAG, "사료 배급 완료.");

    // 배급 완료 상태를 홈서버로 전송
    if (mqtt_client != NULL) {
        esp_mqtt_client_publish(mqtt_client, TOPIC_FEED_STATUS, "FEED_DONE", 0, 1, 0);
    }
}

// 3. MQTT 이벤트 핸들러
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Mosquitto 브로커 연결 성공");
        // 부팅 알림 전송
        esp_mqtt_client_publish(mqtt_client, TOPIC_FEED_STATUS, "ONLINE", 0, 1, 0);
        
        const char *discovery_topic = "homeassistant/button/pet_feeder/config";
        const char *discovery_payload = 
        "{"
            "\"name\":\"사료 배급 버튼\","
            "\"unique_id\":\"esp32_feeder_btn\","
            "\"command_topic\":\"home/feeder/command\","
            "\"payload_press\":\"FEED\","
            "\"icon\":\"mdi:dog\","
            "\"device\":{"
                "\"identifiers\":[\"esp32_pet_feeder\"],"
                "\"name\":\"스마트 사료 배급기\","
                "\"model\":\"ESP32 Continuous Feeder\","
                "\"manufacturer\":\"DIY\""
            "}"
        "}";
        esp_mqtt_client_publish(mqtt_client, discovery_topic, discovery_payload, 0, 1, 1);

        // 제어 토픽 구독 등록
        esp_mqtt_client_subscribe(mqtt_client, TOPIC_FEED_CMD, 0);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "메시지 수신: 토픽 길이=%d, 데이터 길이=%d", event->topic_len, event->data_len);
        
        // 명령 검사 (FEED 문자열 수신 시 구동)
        if (strncmp(event->data, "FEED", event->data_len) == 0) {
            ESP_LOGI(TAG, "배급 명령 접수");
            feed_dispense(FEED_DURATION_MS); // 2초간 토출
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT 연결 해제됨");
        break;

    default:
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// 4. Wi-Fi 연결 처리
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi 재접속 시도...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Wi-Fi 연결 완료. MQTT 시작");
        mqtt_app_start();
    }
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(LED_PIN, LED_OFF);  // 초기 상태는 LED 끄기
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    servo_init();
    led_init();
    wifi_init();
}