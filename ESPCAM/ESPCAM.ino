/**
 * @file config_esp32_cam.h
 * @brief Configuración de hardware, red y utilidades de tiempo para ESP32-CAM
 * 
 * @details
 * Este bloque define:
 * - Credenciales WiFi (para sincronización NTP)
 * - Pines de la cámara ESP32-CAM
 * - Variables globales del sistema
 * - Función para generación de carpetas con timestamp
 */

#include <Arduino.h>
#include <esp_camera.h>
#include <FS.h>
#include <SD_MMC.h>
#include <Suelos_inferencing.h>
#include <WiFi.h>
#include "time.h"
#include "img_converters.h" 

// ================= WIFI =================

/**
 * @brief Nombre de la red WiFi
 * @details Utilizado para conexión a internet y sincronización de hora vía NTP
 */
const char* ssid = "Edison";

/**
 * @brief Contraseña de la red WiFi
 */
const char* password = "012345678";

// ================= PINES ESP32-CAM =================

/**
 * @name Definición de pines ESP32-CAM (AI Thinker)
 * @brief Mapeo de pines para el módulo de cámara
 * @{
 */
#define PWDN_GPIO_NUM     32  ///< Pin de apagado de la cámara
#define RESET_GPIO_NUM    -1  ///< Pin de reset (no utilizado)
#define XCLK_GPIO_NUM      0  ///< Reloj externo de la cámara
#define SIOD_GPIO_NUM     26  ///< SDA (datos SCCB)
#define SIOC_GPIO_NUM     27  ///< SCL (reloj SCCB)

#define Y9_GPIO_NUM       35  ///< Datos bit 9
#define Y8_GPIO_NUM       34  ///< Datos bit 8
#define Y7_GPIO_NUM       39  ///< Datos bit 7
#define Y6_GPIO_NUM       36  ///< Datos bit 6
#define Y5_GPIO_NUM       21  ///< Datos bit 5
#define Y4_GPIO_NUM       19  ///< Datos bit 4
#define Y3_GPIO_NUM       18  ///< Datos bit 3
#define Y2_GPIO_NUM        5  ///< Datos bit 2

#define VSYNC_GPIO_NUM    25  ///< Sincronización vertical
#define HREF_GPIO_NUM     23  ///< Referencia horizontal
#define PCLK_GPIO_NUM     22  ///< Reloj de píxel
/** @} */

/**
 * @brief Pin del botón de captura
 */
#define BUTTON_PIN 13   

// ================= VARIABLES =================

/**
 * @brief Buffer de imagen capturada por la cámara
 * @details Contiene los datos crudos de la imagen
 */
static camera_fb_t *fb = NULL;

/**
 * @brief Resultado de clasificación del modelo
 * @details Almacena la etiqueta del suelo detectado
 */
String suelo_detectado = "no_detectado";

// ================= FUNCIONES =================

/**
 * @brief Genera el nombre de una carpeta basado en fecha y hora actual
 * 
 * @details
 * Utiliza la hora obtenida vía NTP para crear una ruta única
 * donde se almacenarán las imágenes capturadas.
 * 
 * Formato generado:
 * /Muestra_DD-MM-YYYY_HH-MM
 * 
 * Ejemplo:
 * /Muestra_08-04-2026_18-47
 * 
 * @return String Ruta de la carpeta formateada
 * @retval "/sin_fecha" Si no se puede obtener la hora
 */
String getFolderName() {

    struct tm timeinfo;

    if (!getLocalTime(&timeinfo)) {
        return "/sin_fecha";
    }

    char buffer[50];

    strftime(buffer, sizeof(buffer),
             "/Muestra_%d-%m-%Y_%H-%M",
             &timeinfo);

    return String(buffer);
}

/**
 * @brief Inicializa y configura la cámara del módulo ESP32-CAM
 *
 * @details
 * Esta función configura todos los parámetros necesarios para el funcionamiento
 * de la cámara, incluyendo:
 * - Mapeo de pines (AI Thinker ESP32-CAM)
 * - Frecuencia de reloj (XCLK)
 * - Formato de imagen (RGB565)
 * - Resolución (96x96) compatible con Edge Impulse
 *
 * El formato RGB565 es requerido para la correcta conversión de datos hacia
 * el clasificador de Edge Impulse.
 *
 * @note
 * - Se utiliza una sola frame buffer para reducir consumo de memoria.
 * - La resolución está optimizada para inferencia (no para calidad visual).
 *
 * @warning
 * Si la cámara no inicializa correctamente, el sistema no podrá capturar imágenes
 * ni realizar clasificación.
 *
 * @return void
 */
void setup_camera() {

    /// Estructura de configuración de la cámara
    camera_config_t config;

    /// Configuración de temporizador LEDC para señal de reloj
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;

    /// Asignación de pines de datos (D0-D7)
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;

    /// Señales de sincronización y reloj
    config.pin_xclk  = XCLK_GPIO_NUM;  ///< Reloj externo
    config.pin_pclk  = PCLK_GPIO_NUM;  ///< Reloj de píxel
    config.pin_vsync = VSYNC_GPIO_NUM; ///< Sincronización vertical
    config.pin_href  = HREF_GPIO_NUM;  ///< Referencia horizontal

    /// Comunicación SCCB (similar a I2C)
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;

    /// Control de energía
    config.pin_pwdn  = PWDN_GPIO_NUM;  ///< Power down
    config.pin_reset = RESET_GPIO_NUM; ///< Reset (no usado)

    /// Frecuencia del reloj de la cámara (20 MHz típico)
    config.xclk_freq_hz = 20000000;

    /**
     * @brief Configuración de formato de imagen
     * @details
     * RGB565 es necesario para convertir la imagen a formato flotante
     * requerido por Edge Impulse (R, G, B normalizados).
     */
    config.pixel_format = PIXFORMAT_RGB565;

    /**
     * @brief Resolución de captura
     * @details
     * 96x96 coincide con el tamaño de entrada del modelo de clasificación.
     */
    config.frame_size = FRAMESIZE_96X96;

    /// Número de buffers de imagen (1 para bajo consumo)
    config.fb_count = 1;

    /// Inicialización de la cámara
    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("❌ Error cámara");
        return;
    }

    Serial.println("✅ Cámara lista");
}

/**
 * @brief Inicializa todos los componentes del sistema
 *
 * @details
 * Esta función se ejecuta una única vez al iniciar el microcontrolador.
 * Configura los siguientes módulos:
 *
 * - Comunicación serial (para depuración)
 * - Pin del botón de captura
 * - Cámara ESP32-CAM
 * - Tarjeta microSD
 * - Conexión WiFi
 * - Sincronización de hora mediante NTP
 *
 * La conexión WiFi se utiliza exclusivamente para obtener la hora real,
 * la cual se emplea en la creación de carpetas con timestamp.
 *
 * @note
 * - Se usa INPUT_PULLUP para el botón (activo en LOW).
 * - La zona horaria está configurada para Colombia (GMT-5).
 *
 * @warning
 * Si la SD falla, el sistema no podrá almacenar imágenes.
 * Si el WiFi no conecta, no se obtendrá la hora real (NTP).
 *
 * @return void
 */
void setup() {

    /// Inicialización del puerto serial para monitoreo
    Serial.begin(115200);
    delay(2000); ///< Tiempo de estabilización inicial

    /// Configuración del botón como entrada con resistencia pull-up
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    /// Inicialización de la cámara
    setup_camera();

    /**
     * @brief Inicialización de la tarjeta microSD
     * @details
     * Se monta el sistema de archivos en modo 1-bit (true)
     * para mayor compatibilidad con ESP32-CAM.
     */
    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("❌ Error SD");
        return;
    } else {
        Serial.println("✅ SD lista");
    }

    // ================= WIFI + NTP =================

    /**
     * @brief Conexión a la red WiFi
     * @details
     * Necesaria únicamente para sincronizar la hora mediante NTP.
     */
    WiFi.begin(ssid, password);
    Serial.print("Conectando WiFi");

    /// Espera activa hasta establecer conexión
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println(" ✅ conectado");

    /**
     * @brief Configuración del servicio NTP
     * @details
     * Se establece la zona horaria GMT-5 (Colombia) y se utiliza
     * el servidor pool.ntp.org para obtener la hora actual.
     */
    configTime(-5 * 3600, 0, "pool.ntp.org");

    delay(2000); ///< Espera para sincronización inicial
}

/**
 * @brief Bucle principal del sistema de captura y clasificación
 *
 * @details
 * Esta función se ejecuta continuamente y gestiona todo el flujo operativo:
 *
 * 1. Detección de pulsación del botón (flanco descendente)
 * 2. Captura de imagen con la cámara
 * 3. Conversión de imagen a formato compatible con Edge Impulse
 * 4. Ejecución del modelo de clasificación
 * 5. Selección del suelo con mayor probabilidad
 * 6. Conversión de imagen a formato JPEG
 * 7. Creación de carpeta con timestamp (NTP)
 * 8. Almacenamiento de la imagen en la microSD
 *
 * @note
 * - El botón está configurado con INPUT_PULLUP (activo en LOW).
 * - Se utiliza debounce por software (delay de 50 ms).
 * - El sistema usa un umbral de confianza del 60% (0.6).
 *
 * @warning
 * - Si falla la captura de cámara, no se ejecuta el resto del flujo.
 * - Si falla la conversión a JPEG, no se guarda la imagen.
 *
 * @return void
 */
void loop() {

    /**
     * @brief Variables para detección de flanco del botón
     */
    static bool lastState = HIGH;
    bool currentState = digitalRead(BUTTON_PIN);

    /**
     * @brief Detecta transición HIGH → LOW (botón presionado)
     */
    if (lastState == HIGH && currentState == LOW) {

        delay(50); ///< Debounce básico

        if (digitalRead(BUTTON_PIN) != LOW) return;

        Serial.println("📸 Captura activada");

        /**
         * @brief Captura de imagen desde la cámara
         */
        fb = esp_camera_fb_get();

        if (!fb) {
            Serial.println("❌ Error cámara");
            return;
        }

        uint8_t *img = fb->buf;

        // ================= CLASIFICACIÓN =================

        /**
         * @brief Configuración de señal para Edge Impulse
         */
        signal_t signal;

        signal.total_length =
            EI_CLASSIFIER_INPUT_WIDTH *
            EI_CLASSIFIER_INPUT_HEIGHT * 3;

        /**
         * @brief Conversión de RGB565 a RGB normalizado (float)
         *
         * @param offset Índice inicial de lectura
         * @param length Número de elementos a procesar
         * @param out_ptr Buffer de salida (valores normalizados 0-1)
         * @return int Código de estado (0 = OK)
         */
        signal.get_data =
            [&](size_t offset, size_t length, float *out_ptr) -> int {

            size_t pixel_ix = offset;

            for (size_t i = 0; i < length; i += 3) {

                uint16_t pixel =
                    (img[pixel_ix * 2] << 8) |
                    img[pixel_ix * 2 + 1];

                uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                uint8_t g = ((pixel >> 5) & 0x3F) << 2;
                uint8_t b = (pixel & 0x1F) << 3;

                out_ptr[i]     = r / 255.0f;
                out_ptr[i + 1] = g / 255.0f;
                out_ptr[i + 2] = b / 255.0f;

                pixel_ix++;
            }

            return 0;
        };

        /**
         * @brief Resultado del clasificador
         */
        ei_impulse_result_t result = {0};

        /**
         * @brief Ejecución del modelo de Edge Impulse
         */
        if (run_classifier(&signal, &result, false) == EI_IMPULSE_OK) {

            int best_index = 0;
            float best_value = 0;

            /**
             * @brief Evaluación de probabilidades por clase
             */
            for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {

                float prob = result.classification[i].value;

                if (prob > best_value) {
                    best_value = prob;
                    best_index = i;
                }
            }

            /**
             * @brief Selección final basada en umbral de confianza
             */
            if (best_value > 0.6) {
                suelo_detectado =
                    String(result.classification[best_index].label);
            } else {
                suelo_detectado = "no_detectado";
            }
        }

        // ================= CONVERSIÓN A JPEG =================

        /**
         * @brief Buffers para imagen JPEG
         */
        uint8_t *jpg_buf = NULL;
        size_t jpg_len = 0;

        /**
         * @brief Conversión de imagen RGB565 a JPEG
         */
        bool jpeg_converted = fmt2jpg(
            fb->buf,
            fb->len,
            96,
            96,
            PIXFORMAT_RGB565,
            90,
            &jpg_buf,
            &jpg_len
        );

        if (!jpeg_converted) {
            Serial.println("❌ Error al convertir a JPEG");
            esp_camera_fb_return(fb);
            return;
        }

        // ================= GUARDADO EN SD =================

        /**
         * @brief Generación de carpeta con timestamp
         */
        String folder = getFolderName();

        if (!SD_MMC.exists(folder)) {
            SD_MMC.mkdir(folder);
        }

        /**
         * @brief Construcción de ruta del archivo
         */
        String filePath =
            folder + "/" + suelo_detectado + ".jpg";

        /**
         * @brief Escritura de imagen en la microSD
         */
        File file = SD_MMC.open(filePath, FILE_WRITE);

        if (!file) {
            Serial.println("❌ Error al guardar");
        } else {
            file.write(jpg_buf, jpg_len);
            file.close();

            Serial.println("✅ Imagen guardada: " + filePath);
        }

        /**
         * @brief Liberación de memoria
         */
        free(jpg_buf);
        esp_camera_fb_return(fb);

        delay(1500); ///< Evita múltiples capturas consecutivas
    }

    /**
     * @brief Actualización del estado del botón
     */
    lastState = currentState;
}