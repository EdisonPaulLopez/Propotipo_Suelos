/**
 * @file soil_monitor.ino
 * @brief Monitor de humedad del suelo y pH con LCD 16x2 I2C para ESP32.
 *
 * Al presionar el botón, el sistema toma 3 muestras separadas por 5 segundos.
 * Cada muestra lee la humedad del suelo (YL-69) y el pH (sensor analógico),
 * muestra los valores numéricos y su clasificación en una LCD 16x2 I2C,
 * y envía los datos al Monitor Serie.
 *
 * @hardware
 * - ESP32
 * - Sensor de humedad YL-69         → GPIO 32
 * - Sensor de pH analógico          → GPIO 34
 * - Botón pulsador                  → GPIO 19 (INPUT_PULLUP)
 * - LCD 16x2 con módulo I2C         → SDA: GPIO 21 | SCL: GPIO 22
 *
 * @dependencies
 * - Wire.h              (incluida en ESP32 Arduino Core)
 * - LiquidCrystal_I2C   (Frank de Brabander - Arduino Library Manager)
 *
 * @author  Tu Nombre
 * @date    2025
 */

#include <Wire.h>              ///< Comunicación I2C para la LCD
#include <LiquidCrystal_I2C.h> ///< Driver para LCD 16x2 con módulo I2C

// ═══════════════════════════════════════════════════════════════
//  DEFINICIÓN DE PINES
// ═══════════════════════════════════════════════════════════════

#define BUTTON_SEN    19  ///< GPIO del botón pulsador (usa INPUT_PULLUP interno)
#define YL69_PIN      32  ///< GPIO analógico del sensor de humedad YL-69
#define PH_ANALOG_PIN 34  ///< GPIO analógico del sensor de pH

// ═══════════════════════════════════════════════════════════════
//  OBJETO LCD
// ═══════════════════════════════════════════════════════════════

/**
 * @brief Instancia de la LCD 16x2.
 *
 * Parámetros: dirección I2C 0x27, 16 columnas, 2 filas.
 * Si la pantalla no enciende, probar con 0x3F.
 */
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ═══════════════════════════════════════════════════════════════
//  CONFIGURACIÓN DE pH
// ═══════════════════════════════════════════════════════════════

/**
 * @brief Pendiente de la curva de calibración del sensor de pH.
 *
 * Relaciona el cambio de voltaje por unidad de pH.
 * Valor negativo porque a mayor pH el voltaje disminuye.
 * Fórmula: SLOPE = (V_pH4 - V_pH7) / (4.0 - 7.0)
 */
const float SLOPE = -0.1786;

/**
 * @brief Voltaje de referencia del sensor cuando el pH es exactamente 7.0.
 *
 * Se obtiene midiendo el voltaje del sensor sumergido en solución buffer pH 7.
 * Ajustar este valor durante la calibración real del sensor.
 */
const float OFFSET_PH7 = 2.7;

/**
 * @brief Voltaje de referencia del ADC del ESP32 en voltios.
 *
 * El ESP32 con atenuación ADC_11db soporta hasta ~3.3 V en la entrada.
 */
const float ADC_VREF = 3.3;

/**
 * @brief Resolución del ADC en pasos (2^12 = 4096 para 12 bits).
 *
 * El ESP32 usa ADC de 12 bits → valores entre 0 y 4095.
 */
const int ADC_RESOLUTION = 4096;

/**
 * @brief Número de lecturas ADC promediadas por muestra de pH.
 *
 * Promediar múltiples lecturas reduce el ruido eléctrico del sensor analógico.
 */
const int NUM_SAMPLES = 40;

/**
 * @brief Tiempo de espera en ms entre cada lectura ADC del pH.
 *
 * Evita saturar el ADC y permite que la señal se estabilice entre lecturas.
 */
const int SAMPLE_DELAY = 20;

// ═══════════════════════════════════════════════════════════════
//  CONFIGURACIÓN DE MUESTREO
// ═══════════════════════════════════════════════════════════════

/**
 * @brief Cantidad de muestras completas tomadas por ciclo de botón.
 *
 * Cada muestra incluye una lectura de humedad y una de pH.
 */
const int NUM_MUESTRAS = 3;

/**
 * @brief Intervalo en milisegundos entre muestras consecutivas.
 *
 * 5000 ms = 5 segundos. La cuenta regresiva en LCD se basa en este valor.
 */
const int INTERVALO_MS = 5000;

// ═══════════════════════════════════════════════════════════════
//  ESTADO DEL BOTÓN
// ═══════════════════════════════════════════════════════════════

/**
 * @brief Estado lógico del botón en el ciclo anterior del loop().
 *
 * Inicia en HIGH porque el pin usa INPUT_PULLUP (sin presionar = HIGH).
 * Se usa para detectar el flanco de bajada (HIGH → LOW = pulsación).
 */
bool prevBtn = HIGH;

// ═══════════════════════════════════════════════════════════════
//  FUNCIONES AUXILIARES
// ═══════════════════════════════════════════════════════════════

/**
 * @brief Limpia la LCD y escribe un mensaje en ambas filas.
 *
 * Función de conveniencia para evitar repetir lcd.clear() + setCursor()
 * en cada punto del programa donde se actualiza la pantalla.
 *
 * @param linea1  Texto para la fila superior (máx. 16 caracteres).
 * @param linea2  Texto para la fila inferior (máx. 16 caracteres). Por defecto vacío.
 */
void lcdMensaje(const char* linea1, const char* linea2 = "") {
  lcd.clear();           ///< Borra todo el contenido visible de la LCD
  lcd.setCursor(0, 0);   ///< Posiciona el cursor al inicio de la fila 0 (superior)
  lcd.print(linea1);     ///< Escribe el texto de la línea superior
  lcd.setCursor(0, 1);   ///< Posiciona el cursor al inicio de la fila 1 (inferior)
  lcd.print(linea2);     ///< Escribe el texto de la línea inferior
}

/**
 * @brief Muestra en la LCD el resultado numérico y la clasificación combinada.
 *
 * Línea 1 → valores numéricos:  "pH:6.20 Hum:45%"
 * Línea 2 → clasificación:      "Acido & Humedo"
 *
 * Clasificación de pH:
 *   - ph < 6.5  → "Acido"
 *   - ph < 7.5  → "Neutro"
 *   - ph >= 7.5 → "Alcalino"
 *
 * Clasificación de humedad:
 *   - humPct < 30  → "Seco"
 *   - humPct < 70  → "Humedo"
 *   - humPct >= 70 → "Muy humedo"
 *
 * @param ph      Valor de pH calculado (rango 0.0 – 14.0).
 * @param humPct  Porcentaje de humedad del suelo (rango 0 – 100).
 */
void lcdResultado(float ph, int humPct) {

  // ── Selección de etiqueta según el nivel de pH ──────────────
  const char* estadoPH;
  if      (ph < 6.5) estadoPH = "Acido";    ///< pH ácido: menor a 6.5
  else if (ph < 7.5) estadoPH = "Neutro";   ///< pH neutro: entre 6.5 y 7.4
  else               estadoPH = "Alcalino"; ///< pH alcalino: 7.5 o mayor

  // ── Selección de etiqueta según el nivel de humedad ─────────
  const char* estadoHum;
  if      (humPct < 30) estadoHum = "Seco";        ///< Suelo seco: menos del 30%
  else if (humPct < 70) estadoHum = "Humedo";      ///< Suelo húmedo: entre 30% y 69%
  else                  estadoHum = "Muy humedo";  ///< Suelo saturado: 70% o más

  // ── Construcción de la línea 1: valores numéricos ───────────
  char linea1[17]; ///< Buffer de 17 bytes: 16 caracteres + terminador '\0'
  snprintf(linea1, sizeof(linea1), "pH:%.2f Hum:%d%%", ph, humPct);
  ///< %.2f → pH con 2 decimales | %d%% → porcentaje entero (%% imprime '%' literal)

  // ── Construcción de la línea 2: clasificación combinada ─────
  char linea2[17]; ///< Buffer de 17 bytes para la fila inferior
  snprintf(linea2, sizeof(linea2), "%s & %s", estadoPH, estadoHum);
  ///< Ejemplo resultante: "Acido & Humedo"

  // ── Escritura en la LCD ──────────────────────────────────────
  lcd.clear();           ///< Borra la pantalla antes de escribir los nuevos valores
  lcd.setCursor(0, 0);   ///< Cursor al inicio de la fila superior
  lcd.print(linea1);     ///< Imprime "pH:X.XX Hum:XX%"
  lcd.setCursor(0, 1);   ///< Cursor al inicio de la fila inferior
  lcd.print(linea2);     ///< Imprime "EstadoPH & EstadoHum"
}

/**
 * @brief Muestra una cuenta regresiva en la LCD, segundo a segundo.
 *
 * Actualiza la pantalla cada 1000 ms mostrando los segundos restantes.
 * Se llama entre muestras para que el usuario sepa cuánto falta.
 *
 * Ejemplo de pantalla durante la cuenta:
 *   Fila 0: "Prox. muestra en"
 *   Fila 1: "      4 seg..."
 *
 * @param segundos  Número de segundos a contar (normalmente INTERVALO_MS / 1000).
 */
void cuentaRegresiva(int segundos) {
  for (int s = segundos; s > 0; s--) { ///< Itera desde 'segundos' hasta 1
    lcd.clear();                        ///< Limpia la pantalla en cada tick
    lcd.setCursor(0, 0);                ///< Fila superior
    lcd.print("Prox. muestra en");      ///< Mensaje fijo descriptivo
    lcd.setCursor(6, 1);                ///< Fila inferior, centrado aproximadamente
    lcd.print(s);                       ///< Número de segundos restantes
    lcd.print(" seg...");               ///< Unidad y puntos suspensivos
    delay(1000);                        ///< Espera exactamente 1 segundo antes del siguiente tick
  }
}

// ═══════════════════════════════════════════════════════════════
//  FUNCIÓN PRINCIPAL DE MUESTREO
// ═══════════════════════════════════════════════════════════════

/**
 * @brief Ejecuta el ciclo completo de NUM_MUESTRAS lecturas de sensores.
 *
 * Por cada muestra:
 *  1. Indica en LCD y Serie qué muestra se está tomando.
 *  2. Lee el sensor de humedad YL-69 (una lectura ADC).
 *  3. Lee el sensor de pH (promedio de NUM_SAMPLES lecturas ADC).
 *  4. Calcula el voltaje y el pH usando la ecuación de calibración.
 *  5. Muestra resultado numérico + clasificación en la LCD 3 segundos.
 *  6. Si no es la última muestra, ejecuta cuentaRegresiva(5).
 *
 * @note Esta función usa delay() de forma intensiva; el botón no responde
 *       mientras se ejecuta. Para aplicaciones más complejas considerar
 *       una máquina de estados con millis().
 */
void tomarMuestras() {
  for (int m = 1; m <= NUM_MUESTRAS; m++) { ///< Recorre cada una de las 3 muestras

    // ── Aviso de inicio de muestra ───────────────────────────
    char aviso[17]; ///< Buffer temporal para el mensaje de la LCD
    snprintf(aviso, sizeof(aviso), "Muestra %d de %d", m, NUM_MUESTRAS);
    ///< Construye el string dinámicamente, ej: "Muestra 2 de 3"
    lcdMensaje(aviso, "Leyendo...");  ///< Muestra en LCD mientras lee los sensores

    Serial.print("\n===== MUESTRA ");
    Serial.print(m);
    Serial.println(" ====="); ///< Separador visual en el Monitor Serie

    // ── Lectura de humedad ───────────────────────────────────
    int humRaw = analogRead(YL69_PIN);
    ///< Lee el valor crudo del ADC (0–4095): 0 = muy húmedo, 4095 = muy seco

    int humPct = map(humRaw, 0, 4095, 100, 0);
    ///< Convierte el rango ADC a porcentaje inverso:
    ///<   ADC 0    → 100% húmedo
    ///<   ADC 4095 →   0% húmedo (completamente seco)

    Serial.print("[HUM] Crudo: ");
    Serial.print(humRaw);        ///< Valor ADC sin procesar para diagnóstico
    Serial.print(" | Humedad: ");
    Serial.print(humPct);        ///< Porcentaje calculado
    Serial.println(" %");

    // ── Lectura y promediado de pH ───────────────────────────
    long suma = 0; ///< Acumulador de lecturas ADC; long evita desbordamiento con 40 muestras
    for (int i = 0; i < NUM_SAMPLES; i++) {
      suma += analogRead(PH_ANALOG_PIN); ///< Suma cada lectura ADC del sensor de pH
      delay(SAMPLE_DELAY);               ///< Pausa entre lecturas para estabilizar la señal
    }

    int rawADC = suma / NUM_SAMPLES;
    ///< Promedio entero de las 40 lecturas; reduce ruido eléctrico del sensor

    float voltaje = rawADC * (ADC_VREF / ADC_RESOLUTION);
    ///< Convierte el valor ADC a voltaje real:
    ///<   voltaje = rawADC * (3.3 / 4096)

    float ph = 7.0 + (OFFSET_PH7 - voltaje) / (-SLOPE);
    ///< Ecuación de calibración lineal inversa:
    ///<   pH = 7 + (V_pH7 - V_medido) / (-pendiente)
    ///<   Ejemplo: si V=2.5 → pH = 7 + (2.7 - 2.5) / 0.1786 ≈ 8.12

    ph = constrain(ph, 0.0, 14.0);
    ///< Limita el resultado al rango físico válido de pH (0–14)
    ///< Evita mostrar valores absurdos por ruido o sensor desconectado

    Serial.print("[pH]  ADC: ");
    Serial.print(rawADC);          ///< ADC promediado para diagnóstico
    Serial.print(" | V: ");
    Serial.print(voltaje, 3);      ///< Voltaje con 3 decimales
    Serial.print(" | pH: ");
    Serial.print(ph, 2);           ///< pH final con 2 decimales
    Serial.print(" (");
    if      (ph < 6.5) Serial.print("Acido");    ///< pH < 6.5 = ácido
    else if (ph < 7.5) Serial.print("Neutro");   ///< 6.5 ≤ pH < 7.5 = neutro
    else               Serial.print("Alcalino"); ///< pH ≥ 7.5 = alcalino
    Serial.println(")");

    // ── Mostrar resultado en LCD ─────────────────────────────
    delay(500);              ///< Breve pausa para que el usuario note el cambio de pantalla
    lcdResultado(ph, humPct); ///< Fila 0: "pH:X.XX Hum:XX%" | Fila 1: "Estado & Estado"
    delay(3000);             ///< Mantiene el resultado visible 3 segundos antes de continuar

    // ── Cuenta regresiva entre muestras ─────────────────────
    if (m < NUM_MUESTRAS) {           ///< No ejecutar cuenta regresiva después de la última muestra
      cuentaRegresiva(INTERVALO_MS / 1000); ///< Divide ms a segundos → 5000 / 1000 = 5 seg
    }
  }

  // ── Fin del ciclo de muestreo ────────────────────────────
  Serial.println("\n===== MUESTREO COMPLETO =====\n"); ///< Cierre en Monitor Serie
  lcdMensaje("Listo!", "Presiona boton");               ///< Vuelve al estado de espera
}

// ═══════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════

/**
 * @brief Inicializa periféricos y comunicaciones al arrancar el ESP32.
 *
 * Orden de inicialización:
 *  1. Monitor Serie a 115200 baudios.
 *  2. LCD: init + backlight + mensaje de bienvenida.
 *  3. Botón con resistencia pull-up interna.
 *  4. ADC: resolución 12 bits y atenuación 11 dB (rango 0–3.3 V).
 */
void setup() {
  Serial.begin(115200); ///< Inicia comunicación serie a 115200 baudios con el PC

  lcd.init();           ///< Inicializa el controlador I2C de la LCD
  lcd.backlight();      ///< Enciende la retroiluminación de la LCD
  lcdMensaje("Sistema listo", "Presiona boton"); ///< Pantalla de bienvenida al usuario

  pinMode(BUTTON_SEN, INPUT_PULLUP);
  ///< Configura el pin del botón con pull-up interno:
  ///<   Sin presionar → HIGH (3.3 V)
  ///<   Presionado    → LOW  (0 V, conectado a GND)

  analogReadResolution(12);
  ///< Configura el ADC del ESP32 en modo 12 bits → valores entre 0 y 4095

  analogSetAttenuation(ADC_11db);
  ///< Habilita atenuación 11 dB en todos los pines analógicos:
  ///<   Permite leer voltajes de 0 hasta ~3.3 V sin saturar el ADC

  Serial.println("Sistema listo. Presiona el boton para medir...");
}

// ═══════════════════════════════════════════════════════════════
//  LOOP PRINCIPAL
// ═══════════════════════════════════════════════════════════════

/**
 * @brief Bucle principal: detecta pulsación del botón por flanco de bajada.
 *
 * Técnica de detección por flanco (edge detection):
 *   Solo se activa en el momento exacto en que el botón pasa de HIGH a LOW,
 *   ignorando el tiempo que permanece presionado. Esto evita múltiples
 *   activaciones por una sola pulsación.
 *
 *   Condición: prevBtn == HIGH && currBtn == LOW
 *     ↑ estaba suelto       ↑ ahora está presionado → flanco de bajada
 */
void loop() {
  bool currBtn = digitalRead(BUTTON_SEN);
  ///< Lee el estado actual del botón en este ciclo del loop

  if (prevBtn == HIGH && currBtn == LOW) {
    ///< Flanco de bajada detectado: el botón acaba de ser presionado
    Serial.println("Boton presionado. Iniciando muestreo...");
    lcdMensaje("Iniciando...", ""); ///< Feedback inmediato al usuario en la LCD
    delay(500);                     ///< Anti-rebote básico: ignora rebotes del primer medio segundo
    tomarMuestras();                ///< Ejecuta el ciclo completo de 3 muestras
  }

  prevBtn = currBtn;
  ///< Guarda el estado actual para compararlo en el próximo ciclo del loop
}