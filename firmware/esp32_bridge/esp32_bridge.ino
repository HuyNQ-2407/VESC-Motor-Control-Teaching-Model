#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// WiFi
#define WIFI_SSID           "PAB"
#define WIFI_PASSWORD       "12345678"
#define TCP_PORT            8888

// UART to VESC
#define VESC_UART_NUM       2
#define VESC_RX_PIN         16
#define VESC_TX_PIN         17
#define VESC_BAUD_RATE      115200
#define VESC_RX_BUFFER_SIZE 2048

// Hall sensor (2 channels)
#define HALL1_PIN           27      // H1: RPM count (rising edge)
#define HALL2_PIN           26      // H2: direction
#define PULSES_PER_REV      2       // 4 poles = 2 pole pairs
#define HALL_DEBOUNCE_US    200
#define HALL_WINDOW_MS      300     // RPM calculation window

#ifndef LED_BUILTIN
#define LED_BUILTIN         2
#endif

// Timing
#define REMOTE_TIMEOUT_MS   3000
#define LCD_UPDATE_MS       500
#define IP_DISPLAY_MS       10000
#define TCP_HEALTH_CHECK_MS 1000
#define DATA_TIMEOUT_MS     50

LiquidCrystal_I2C lcd(0x27, 20, 4);
WiFiServer tcpServer(TCP_PORT);
WiFiClient tcpClient;
HardwareSerial vescUART(VESC_UART_NUM);

bool clientConnected = false;
bool showingIP = false;
unsigned long lastCommandTime = 0;
unsigned long lastLCDupdate = 0;
unsigned long wifiConnectTime = 0;
unsigned long lastTCPHealthCheck = 0;

struct VESCData {
    float voltage = 0.0;
    float current = 0.0;
    float duty = 0.0;
    float rpm_ref = 0.0;
    float rpm_vesc = 0.0;
    float temp_mosfet = 0.0;
    float temp_motor = 0.0;
    unsigned long timestamp = 0;
};
VESCData currentData;
String direction = "FWD";

// Hall sensor state (2-channel quadrature)
volatile uint32_t hallCount = 0;
unsigned long lastHallWindow = 0;
int rpm_hall = 0;

// Direction detection
volatile uint8_t prevCode = 0xFF;    // (H2<<1)|H1, previous
volatile uint8_t lastH1 = 0xFF;      // H1 level, previous
volatile int8_t dirAcc = 0;          // direction accumulator (+FWD / -REV)
int8_t hallDir = 0;                  // -1 REV, 0 STOP, +1 FWD
String hallDirStr = "STOP";

// Quadrature direction lookup table
int8_t dirLUT[4][4] = {
/* prev -> curr:    00  01  10  11 */
 /* 00 */          {  0, +1, -1,  0 },
 /* 01 */          { -1,  0,  0, +1 },
 /* 10 */          { +1,  0,  0, -1 },
 /* 11 */          {  0, -1, +1,  0 }
};

// UART ring buffer
#define UART_RING_SIZE 512
char uartRing[UART_RING_SIZE];
volatile int writeIdx = 0;
volatile int readIdx = 0;

inline void handleHallEdges() {
    static unsigned long last_us = 0;
    unsigned long now = micros();
    if (now - last_us < HALL_DEBOUNCE_US) return;
    last_us = now;

    uint8_t h1 = digitalRead(HALL1_PIN) & 1;
    uint8_t h2 = digitalRead(HALL2_PIN) & 1;
    uint8_t code = (h2 << 1) | h1;

    // RPM count: rising edge of H1
    uint8_t pH1 = lastH1;
    lastH1 = h1;
    if (pH1 != 0xFF && pH1 == 0 && h1 == 1) {
        hallCount++;
    }

    // Direction from quadrature transition
    uint8_t p = prevCode;
    prevCode = code;
    if (p <= 3 && code <= 3 && p != code) {
        int8_t step = dirLUT[p][code]; // +1 FWD, -1 REV, 0 invalid
        if (step != 0) dirAcc += step;
    }
}

void IRAM_ATTR H1_ISR_CHANGE() { handleHallEdges(); }
void IRAM_ATTR H2_ISR_CHANGE() { handleHallEdges(); }

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\nESP32 VESC Bridge");
    Serial.println("- Non-blocking TCP");
    Serial.println("- Ring buffer UART");
    Serial.println("- 2-channel Hall with direction");
    Serial.println("- Timestamped data");

    pinMode(LED_BUILTIN, OUTPUT);

    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(2, 0);
    lcd.print("VESC CONTROLLER");
    lcd.setCursor(2, 2);
    lcd.print("Connecting WiFi");
    lcd.setCursor(3, 3);
    lcd.print("Please wait...");

    pinMode(HALL1_PIN, INPUT_PULLUP);
    pinMode(HALL2_PIN, INPUT_PULLUP);

    lastH1   = digitalRead(HALL1_PIN) & 1;
    prevCode = ((digitalRead(HALL2_PIN) & 1) << 1) | (lastH1 & 1);

    attachInterrupt(digitalPinToInterrupt(HALL1_PIN), H1_ISR_CHANGE, CHANGE);
    attachInterrupt(digitalPinToInterrupt(HALL2_PIN), H2_ISR_CHANGE, CHANGE);

    Serial.printf("Hall Sensor: H1=%d H2=%d Pairs=%d Window=%dms\n",
                  HALL1_PIN, HALL2_PIN, PULSES_PER_REV, HALL_WINDOW_MS);

    vescUART.setRxBufferSize(VESC_RX_BUFFER_SIZE);
    vescUART.begin(VESC_BAUD_RATE, SERIAL_8N1, VESC_RX_PIN, VESC_TX_PIN);
    Serial.printf("VESC UART: RX=%d TX=%d @ %d baud (Buffer: %d bytes)\n",
                  VESC_RX_PIN, VESC_TX_PIN, VESC_BAUD_RATE, VESC_RX_BUFFER_SIZE);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.printf("Connected! IP: %s:%d\n", WiFi.localIP().toString().c_str(), TCP_PORT);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1);
    lcd.print("IP: ");
    lcd.print(WiFi.localIP().toString());
    lcd.setCursor(0, 2);
    lcd.print("Port: ");
    lcd.print(TCP_PORT);
    lcd.setCursor(0, 3);
    lcd.print("Use IP for APP");

    wifiConnectTime = millis();
    showingIP = true;

    tcpServer.begin();
    tcpServer.setNoDelay(true);

    lastHallWindow = millis();
}

void loop() {
    unsigned long now = millis();

    if (showingIP && (now - wifiConnectTime > IP_DISPLAY_MS)) {
        showingIP = false;
        lcd.clear();
    }

    handleTCPConnection();
    processUARTData(); // priority: keep the UART ring buffer drained

    if (clientConnected) {
        processTCPCommands();
    }

    calculateHallRPM();

    if (!showingIP && (now - lastLCDupdate > LCD_UPDATE_MS)) {
        updateLCD();
        lastLCDupdate = now;
    }

    static unsigned long lastBlink = 0;
    if (now - lastBlink > (clientConnected ? 500 : 2000)) {
        lastBlink = now;
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }

    yield();
}

void handleTCPConnection() {
    if (!clientConnected) {
        if (tcpServer.hasClient()) {
            tcpClient = tcpServer.available();
            if (tcpClient) {
                clientConnected = true;
                lastCommandTime = millis();
                tcpClient.setNoDelay(true);

                Serial.println("Qt App connected!");
                tcpClient.println("Connected to VESC Bridge");
            }
        }
    } else {
        if (!tcpClient.connected()) {
            Serial.println("Client disconnected");
            tcpClient.stop();
            clientConnected = false;
            vescUART.println("remote off");
            return;
        }

        unsigned long now = millis();
        if (now - lastTCPHealthCheck > TCP_HEALTH_CHECK_MS) {
            lastTCPHealthCheck = now;

            int available = tcpClient.availableForWrite();
            if (available < 256) {
                Serial.printf("WARNING: TCP TX buffer low: %d bytes\n", available);
            }
        }
    }
}

void processUARTData() {
    int available = vescUART.available();
    int processed = 0;

    while (available > 0 && processed < 128) {
        int nextWrite = (writeIdx + 1) % UART_RING_SIZE;
        if (nextWrite == readIdx) break;

        uartRing[writeIdx] = vescUART.read();
        writeIdx = nextWrite;
        available--;
        processed++;
    }

    static String lineBuffer = "";
    static unsigned long lastDataTime = 0;
    lineBuffer.reserve(128);

    while (readIdx != writeIdx) {
        char c = uartRing[readIdx];
        readIdx = (readIdx + 1) % UART_RING_SIZE;

        if (c == '\n') {
            unsigned long now = millis();

            if (parseVESCData(lineBuffer, now)) {
                if (now - lastDataTime >= DATA_TIMEOUT_MS) {
                    sendDataToClient(lineBuffer);
                    lastDataTime = now;
                }
            } else if (clientConnected) {
                tcpClient.println(lineBuffer);
            }

            lineBuffer = "";
        } else if (c != '\r' && lineBuffer.length() < 120) {
            lineBuffer += c;
        }
    }
}

bool parseVESCData(const String& data, unsigned long timestamp) {
    int commaCount = 0;
    for (int i = 0; i < data.length(); i++) {
        if (data[i] == ',') commaCount++;
    }

    if (commaCount != 6) return false;

    int idx[7] = {0};
    int pos = 0;

    for (int i = 0; i < 6; i++) {
        idx[i] = data.indexOf(',', pos);
        if (idx[i] == -1) return false;
        pos = idx[i] + 1;
    }
    idx[6] = data.length();

    currentData.voltage = data.substring(0, idx[0]).toFloat();
    currentData.current = data.substring(idx[0] + 1, idx[1]).toFloat();
    currentData.duty = data.substring(idx[1] + 1, idx[2]).toFloat();
    currentData.rpm_ref = data.substring(idx[2] + 1, idx[3]).toFloat();
    currentData.rpm_vesc = data.substring(idx[3] + 1, idx[4]).toFloat();
    currentData.temp_mosfet = data.substring(idx[4] + 1, idx[5]).toFloat();
    currentData.temp_motor = data.substring(idx[5] + 1, idx[6]).toFloat();
    currentData.timestamp = timestamp;

    direction = (currentData.rpm_ref >= 0) ? "FWD" : "REV";

    return true;
}

void sendDataToClient(const String& data) {
    if (!clientConnected) return;

    int written = tcpClient.print(data);
    tcpClient.print('\n');

    if (written == 0) {
        Serial.println("WARNING: TCP write failed");
    }
}

void processTCPCommands() {
    if (!tcpClient.available()) return;

    String command = tcpClient.readStringUntil('\n');
    command.trim();

    if (command.length() > 0) {
        lastCommandTime = millis();
        vescUART.println(command);

        if (!command.startsWith("setrpm")) {
            Serial.printf("Qt -> VESC: %s\n", command.c_str());
        }
    }
}

void calculateHallRPM() {
    unsigned long now = millis();

    if (now - lastHallWindow >= HALL_WINDOW_MS) {
        lastHallWindow = now;

        noInterrupts();
        uint32_t n = hallCount;
        hallCount = 0;
        int8_t acc = dirAcc;
        dirAcc = 0;
        interrupts();

        int rpmNew = 0;
        if (n > 0) {
            rpmNew = (int)((60.0f * n * 1000.0f) / (PULSES_PER_REV * (float)HALL_WINDOW_MS));
        }

        // Simple exponential smoothing
        rpm_hall = (rpm_hall == 0) ? rpmNew : (int)(rpm_hall * 0.6f + rpmNew * 0.4f);

        // Require |acc| >= 2 before confirming direction, to reject jitter
        if (acc >= 2) {
            hallDir = +1;
            hallDirStr = "FWD";
        } else if (acc <= -2) {
            hallDir = -1;
            hallDirStr = "REV";
        } else if (rpm_hall == 0) {
            hallDir = 0;
            hallDirStr = "STOP";
        }

        if (rpm_hall > 0 || hallDir != 0) {
            Serial.printf("Hall: n=%u rpm=%d acc=%d dir=%s\n",
                         n, rpm_hall, acc, hallDirStr.c_str());
        }
    }
}

void updateLCD() {
    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("B:");
    lcd.print(currentData.voltage, 2);
    lcd.print("V");

    lcd.setCursor(10, 0);
    lcd.print("I:");
    lcd.print(abs(currentData.current), 2);
    lcd.print("A");

    lcd.setCursor(0, 2);
    lcd.print("RPM:");
    lcd.print(abs(currentData.rpm_vesc), 0);

    lcd.setCursor(0, 3);
    lcd.print("Ref:");
    lcd.print(abs(currentData.rpm_ref), 0);

    lcd.setCursor(0, 1);
    lcd.print("Duty:");
    lcd.print(abs(currentData.duty * 100), 0);
    lcd.print("%");

    lcd.setCursor(10, 1);
    lcd.print(direction);

    lcd.setCursor(10, 2);
    lcd.print("Hall:");
    lcd.print(rpm_hall, 0);

    lcd.setCursor(10, 3);
    lcd.print(clientConnected ? "App:OK" : "App:--");
}
