#include "arduino_secrets.h"
#include <Wire.h>
#include <M5GFX.h>
#include <M5CoreS3.h>
#include <M5Unified.h>
#include <M5UNIT_NCIR2.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "thingProperties.h"  // Include the thingProperties.h file

// Initialize objects
M5UNIT_NCIR2 ncir2;
Preferences preferences;

// Configuration namespace
namespace Config {
    // Display settings
    namespace Display {
        const uint32_t COLOR_BACKGROUND = TFT_BLACK;
        const uint32_t COLOR_TEXT = TFT_WHITE;
        const uint32_t COLOR_PRIMARY = 0x3CD070;    // Main theme color (bright green)
        const uint32_t COLOR_SECONDARY = 0x1B3358;  // Secondary color (muted blue)
        const uint32_t COLOR_WARNING = 0xFFB627;    // Warning color (amber)
        const uint32_t COLOR_ERROR = 0xFF0F7B;      // Error color (pink)
        const uint32_t COLOR_SUCCESS = 0x3CD070;    // Success color (green)
        const uint32_t COLOR_ICE = 0x87CEFA;     // Light blue for too cold
        const uint32_t COLOR_READY = 0x00FF00;   // Neon green for ready
        const uint32_t COLOR_LAVA = 0xFF4500;    // Bright red-orange for way too hot
        
        // Layout constants
        const int HEADER_HEIGHT = 30;
        const int FOOTER_HEIGHT = 40;
        const int STATUS_HEIGHT = 40;
        const int MARGIN = 10;
        const int BUTTON_HEIGHT = 50;
    }
    
    // Temperature settings
    namespace Temperature {
        const float MIN_TEMP_C = -20;   // Minimum valid temperature
        const float MAX_TEMP_C = 400;   // Maximum valid temperature
        const float DEFAULT_TARGET_C = 180.0;
        const float DEFAULT_TOLERANCE_C = 5.0;
        const unsigned long UPDATE_INTERVAL_MS = 2000;  // Changed to 5 seconds for serial monitor
        
        // Temperature ranges in Celsius
        const int16_t TEMP_MIN_C = 304;     // 304°C (580°F)
        const int16_t TEMP_MAX_C = 371;     // 371°C (700°F)
        const int16_t TEMP_TARGET_C = 338;  // 338°C (640°F)
        const int16_t TEMP_TOLERANCE_C = 5;  // 5°C

        // Temperature ranges in Fahrenheit
        const int16_t TEMP_MIN_F = 580;     // 580°F
        const int16_t TEMP_MAX_F = 700;     // 700°F
        const int16_t TEMP_TARGET_F = 640;  // 640°F
        const int16_t TEMP_TOLERANCE_F = 9;  // 9°F
    }
    
    // System settings
    namespace System {
        const unsigned long WATCHDOG_TIMEOUT_MS = 30000;
        const unsigned long BATTERY_CHECK_INTERVAL_MS = 5000;
        const int DEFAULT_BRIGHTNESS = 128;
        const float DEFAULT_EMISSIVITY = 0.87;
    }
}

// Settings structure
struct Settings {
    bool useCelsius = true;
    bool soundEnabled = true;
    int brightness = Config::System::DEFAULT_BRIGHTNESS;
    float emissivity = Config::System::DEFAULT_EMISSIVITY;
    float targetTemp = Config::Temperature::DEFAULT_TARGET_C;
    float tempTolerance = Config::Temperature::DEFAULT_TOLERANCE_C;
    
    void load() {
        preferences.begin("terpmeter", false);
        useCelsius = preferences.getBool("useCelsius", true);
        soundEnabled = preferences.getBool("soundEnabled", true);
        brightness = preferences.getInt("brightness", Config::System::DEFAULT_BRIGHTNESS);
        emissivity = preferences.getFloat("emissivity", Config::System::DEFAULT_EMISSIVITY);
        targetTemp = preferences.getFloat("targetTemp", Config::Temperature::DEFAULT_TARGET_C);
        tempTolerance = preferences.getFloat("tolerance", Config::Temperature::DEFAULT_TOLERANCE_C);
        preferences.end();
    }
    
    void save() {
        preferences.begin("terpmeter", false);
        preferences.putBool("useCelsius", useCelsius);
        preferences.putBool("soundEnabled", soundEnabled);
        preferences.putInt("brightness", brightness);
        preferences.putFloat("emissivity", emissivity);
        preferences.putFloat("targetTemp", targetTemp);
        preferences.putFloat("tolerance", tempTolerance);
        preferences.end();
    }
};

// System state structure
struct SystemState {
    bool isMonitoring = false;
    float currentTemp = 0.0f;
    unsigned long lastTempUpdate = 0;
    unsigned long lastSerialUpdate = 0;  // Added separate timer for serial updates
    unsigned long lastBatteryCheck = 0;
    bool lowBatteryWarning = false;
    String statusMessage;
    uint32_t statusColor = Config::Display::COLOR_TEXT;
    enum Screen { MAIN, SETTINGS } currentScreen = MAIN;
};

// Settings menu state
struct SettingsMenu {
    bool isOpen = false;
    int selectedItem = 0;
    const int numItems = 6;
    const char* menuItems[6] = {
        "Temperature Unit",
        "Sound",
        "Brightness",
        "Emissivity",
        "Target Temp",
        "Tolerance"
    };
};

SettingsMenu settingsMenu;

// Global instances
Settings settings;
SystemState state;

// Forward declarations
void playSound(bool success);

// UI Components
class Button {
public:
    int x, y, width, height;
    String label;
    bool enabled;
    bool pressed;
    bool isToggle;
    bool toggleState;
    
    Button(int _x, int _y, int _w, int _h, String _label, bool _enabled = true, bool _isToggle = false)
        : x(_x), y(_y), width(_w), height(_h), label(_label), enabled(_enabled), 
          pressed(false), isToggle(_isToggle), toggleState(false) {}
    
    bool contains(int touch_x, int touch_y) {
        return enabled && 
               touch_x >= x && touch_x < x + width &&
               touch_y >= y && touch_y < y + height;
    }
    
    void draw(uint32_t color = Config::Display::COLOR_SECONDARY) {
        if (!enabled) {
            color = Config::Display::COLOR_SECONDARY;
        } else if (isToggle && toggleState) {
            color = Config::Display::COLOR_PRIMARY;
        }
        
        // Draw button background with pressed effect
        if (pressed) {
            // Draw darker background when pressed
            uint32_t pressedColor = (color == Config::Display::COLOR_PRIMARY) ? 
                                  Config::Display::COLOR_READY : 
                                  Config::Display::COLOR_WARNING;
            CoreS3.Display.fillRoundRect(x, y, width, height, 8, pressedColor);
            
            // Draw slightly offset text for 3D effect
            CoreS3.Display.setTextSize(2);  // Increased size for main menu buttons
            CoreS3.Display.setTextDatum(middle_center);
            CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
            CoreS3.Display.drawString(label.c_str(), x + width/2 + 1, y + height/2 + 1);
        } else {
            // Normal button drawing
            CoreS3.Display.fillRoundRect(x, y, width, height, 8, color);
            
            // Draw button border
            CoreS3.Display.drawRoundRect(x, y, width, height, 8, Config::Display::COLOR_TEXT);
            
            // Draw label
            CoreS3.Display.setTextSize(2);  // Increased size for main menu buttons
            CoreS3.Display.setTextDatum(middle_center);
            CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
            CoreS3.Display.drawString(label.c_str(), x + width/2, y + height/2);
        }
    }
    
    void setPressed(bool isPressed) {
        if (pressed != isPressed) {
            pressed = isPressed;
            if (pressed && settings.soundEnabled) {
                playSound(true);
            }
            draw(isToggle && toggleState ? Config::Display::COLOR_PRIMARY : Config::Display::COLOR_SECONDARY);
        }
    }
    
    void setToggleState(bool state) {
        if (toggleState != state) {
            toggleState = state;
            draw();
        }
    }
};

// Forward declarations of helper functions
void updateDisplay();
void handleTouch();
void checkTemperature();
void checkBattery();
void updateStatus(const String& message, uint32_t color);
void drawHeader();
void drawFooter();
void drawMainDisplay();
void drawSettingsMenu();
void handleSettingsTouch(int x, int y);
void drawTemperature();
void adjustEmissivity();

bool isValidTemperature(float temp) {
    // Check if temperature is within reasonable bounds (-20°C to 200°C)
    return temp > -2000 && temp < 20000;  // Values are in centidegrees
}

// Main setup function
void setup() {
    Serial.begin(115200);
    
    // Initialize Core S3 with configuration
    auto cfg = M5.config();
    CoreS3.begin(cfg);
    
    // Initialize power management
    CoreS3.Power.begin();
    CoreS3.Power.setChargeCurrent(900);
    CoreS3.Power.setExtOutput(true);  // Set to false when using Grove port
    delay(500);  // Give power time to stabilize
    
    esp_task_wdt_init(Config::System::WATCHDOG_TIMEOUT_MS / 1000, true);
    esp_task_wdt_add(NULL);
    
    // Load settings first
    settings.load();
    
    // Initialize display settings
    CoreS3.Display.setRotation(1);
    CoreS3.Display.setBrightness(settings.brightness);
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    
    // Initialize I2C and sensor
    int retryCount = 0;
    bool sensorInitialized = false;
    bool validReadingObtained = false;
    
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Initializing Sensor", CoreS3.Display.width()/2, 40);
    
    // Initialize I2C after power is stable
    Wire.end();
    Wire.begin(2, 1, 400000);
    delay(100);  // Wait for I2C to stabilize
    
    while (!sensorInitialized && retryCount < 5) {  // Increased max retries
        Wire.end();
        delay(250);  // Increased delay
        Wire.begin(2, 1, 400000);
        delay(1000);  // Longer delay after Wire.begin
        
        CoreS3.Display.drawString("Connecting to Sensor", CoreS3.Display.width()/2, 80);
        CoreS3.Display.drawString("Attempt " + String(retryCount + 1) + "/5", 
                                CoreS3.Display.width()/2, 120);
        
        if (ncir2.begin(&Wire, 2, 1, M5UNIT_NCIR2_DEFAULT_ADDR)) {
            // Try to get valid readings
            int validReadingCount = 0;
            const int requiredValidReadings = 3;  // Need 3 valid readings to proceed
            
            CoreS3.Display.drawString("Checking Sensor...", CoreS3.Display.width()/2, 160);
            
            ncir2.setEmissivity(settings.emissivity);  // Use emissivity from settings
            ncir2.setConfig();
            delay(500);  // Wait after config
            
            for (int i = 0; i < 10 && validReadingCount < requiredValidReadings; i++) {
                float temp = ncir2.getTempValue();
                Serial.printf("Init reading %d: %.2f\n", i, temp);
                
                if (isValidTemperature(temp)) {
                    validReadingCount++;
                    CoreS3.Display.drawString("Valid Reading " + String(validReadingCount) + "/" + String(requiredValidReadings), 
                                           CoreS3.Display.width()/2, 200);
                } else {
                    validReadingCount = 0;  // Reset if we get an invalid reading
                }
                delay(200);
            }
            
            if (validReadingCount >= requiredValidReadings) {
                sensorInitialized = true;
                validReadingObtained = true;
                CoreS3.Display.drawString("Sensor Ready!", CoreS3.Display.width()/2, 240);
                delay(1000);
            } else {
                Serial.println("Failed to get valid readings");
            }
        }
        
        if (!sensorInitialized) {
            retryCount++;
            delay(500);
        }
    }
    
    if (!sensorInitialized || !validReadingObtained) {
        CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
        CoreS3.Display.setTextColor(Config::Display::COLOR_ERROR);
        CoreS3.Display.drawString("Sensor Init Failed!", CoreS3.Display.width()/2, CoreS3.Display.height()/2);
        while (1) {
            if (CoreS3.Power.isCharging()) ESP.restart();
            delay(100);
        }
    }
    
    // Initialize display for normal operation
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.setRotation(1);
    CoreS3.Display.setBrightness(settings.brightness);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.setTextSize(1);
    
    // Draw initial display
    updateDisplay();

    // Initialize Arduino IoT Cloud
    initProperties();
    ArduinoCloud.begin(ArduinoIoTPreferredConnection);
    setDebugMessageLevel(4);
    ArduinoCloud.printDebugInfo();
}

// Main loop function
void loop() {
    ArduinoCloud.update();
    esp_task_wdt_reset();

    // Update M5Stack core - needed for touch events
    CoreS3.update();

    unsigned long currentMillis = millis();
    
    // Handle touch events
    handleTouch();

    // Check temperature continuously but only update display on significant changes
    float temp = readTemperature();
    
    // Update serial monitor on time interval
    if (currentMillis - state.lastSerialUpdate >= Config::Temperature::UPDATE_INTERVAL_MS) {
        float tempF = (temp * 9.0/5.0) + 32.0;
        Serial.println("\nTemperature Readings:");
        Serial.printf("  Raw: %.2f\n", temp * 100.0);
        Serial.printf("  Celsius: %.2f°C\n", temp);
        Serial.printf("  Fahrenheit: %.2f°F\n", tempF);
        Serial.printf("  Display Value: %d°F\n", (int)round(tempF));
        state.lastSerialUpdate = currentMillis;
    }
    
    // Update display only on significant changes
    if (abs(temp - state.currentTemp) >= 1.0 || state.currentTemp == 0) {
        state.currentTemp = temp;
        state.lastTempUpdate = currentMillis;  // Update temp timestamp
        
        if (state.isMonitoring) {
            float diff = abs(temp - settings.targetTemp);
            if (diff > settings.tempTolerance) {
                updateStatus("Temperature Out of Range!", Config::Display::COLOR_WARNING);
                if (settings.soundEnabled) {
                    playSound(false);
                }
            } else {
                updateStatus("Monitoring", Config::Display::COLOR_SUCCESS);
            }
        }

        // Update the Arduino Cloud property
        float tempF = (temp * 9.0 / 5.0) + 32.0;
        temperature = tempF;

        // Only update temperature display if not in settings menu
        if (!settingsMenu.isOpen) {
            drawTemperature();  // Just update the temperature area
        }
    }

    // Check battery status
    if (currentMillis - state.lastBatteryCheck >= Config::System::BATTERY_CHECK_INTERVAL_MS) {
        checkBattery();
        state.lastBatteryCheck = currentMillis;
    }
    
    // Small delay to prevent overwhelming the system
    delay(10);
}

void checkTemperature() {
    // This function is now just a wrapper for temperature reading
    float temp = readTemperature();
    if (isValidTemperature(temp)) {
        state.currentTemp = temp;
    }
}

void handleTouch() {
    static Button* lastPressedButton = nullptr;
    static bool touchProcessed = false;
    
    if (CoreS3.Touch.getCount()) {
        auto touch = CoreS3.Touch.getDetail();
        
        // Handle new touch press
        if (touch.wasPressed() && !touchProcessed) {
            touchProcessed = true;
            
            // Check footer buttons (always active)
            int footerY = CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT;
            if (touch.y >= footerY) {
                int buttonWidth = (CoreS3.Display.width() - 3 * Config::Display::MARGIN) / 2;
                
                // Monitor button (only when not in settings)
                if (!settingsMenu.isOpen && 
                    touch.x >= Config::Display::MARGIN && 
                    touch.x < Config::Display::MARGIN + buttonWidth) {
                    
                    Button monitorBtn(Config::Display::MARGIN, 
                                   footerY + (Config::Display::FOOTER_HEIGHT - Config::Display::BUTTON_HEIGHT) / 2,
                                   buttonWidth, 
                                   Config::Display::BUTTON_HEIGHT - 4,
                                   state.isMonitoring ? "Stop" : "Monitor",
                                   true, true);
                    monitorBtn.toggleState = state.isMonitoring;
                    monitorBtn.setPressed(true);
                    lastPressedButton = new Button(monitorBtn);
                }
                // Settings/Back button (always active)
                else if (touch.x >= CoreS3.Display.width() - buttonWidth - Config::Display::MARGIN && 
                         touch.x < CoreS3.Display.width() - Config::Display::MARGIN) {
                    
                    Button settingsBtn(CoreS3.Display.width() - buttonWidth - Config::Display::MARGIN,
                                     footerY + (Config::Display::FOOTER_HEIGHT - Config::Display::BUTTON_HEIGHT) / 2,
                                     buttonWidth,
                                     Config::Display::BUTTON_HEIGHT - 4,
                                     settingsMenu.isOpen ? "Back" : "Settings");
                    settingsBtn.setPressed(true);
                    lastPressedButton = new Button(settingsBtn);
                }
            }
            // Handle settings menu touches only when in settings
            else if (settingsMenu.isOpen) {
                handleSettingsTouch(touch.x, touch.y);
            }
        }
        // Handle release
        else if (touch.wasReleased() && lastPressedButton != nullptr && touchProcessed) {
            touchProcessed = false;
            
            // Handle button release
            if (lastPressedButton->contains(touch.x, touch.y)) {
                // Monitor button
                if (lastPressedButton->label == "Monitor" || lastPressedButton->label == "Stop") {
                    state.isMonitoring = !state.isMonitoring;
                    // Control LED based on monitoring state
                    ncir2.setLEDColor(state.isMonitoring ? 0xFFFFFF : 0);  // White when on, off when not monitoring
                }
                // Settings button
                else if (lastPressedButton->label == "Settings" || lastPressedButton->label == "Back") {
                    settingsMenu.isOpen = !settingsMenu.isOpen;
                    state.currentScreen = settingsMenu.isOpen ? SystemState::SETTINGS : SystemState::MAIN;
                }
            }
            
            // Reset button state and cleanup
            lastPressedButton->setPressed(false);
            delete lastPressedButton;
            lastPressedButton = nullptr;
            
            // Update display
            updateDisplay();
        }
    } else {
        // Reset touch state when no touches are detected
        touchProcessed = false;
        if (lastPressedButton != nullptr) {
            lastPressedButton->setPressed(false);
            delete lastPressedButton;
            lastPressedButton = nullptr;
        }
    }
}

void updateDisplay() {
    // Clear the entire content area (between header and footer)
    int contentY = Config::Display::HEADER_HEIGHT;
    int contentHeight = CoreS3.Display.height() - Config::Display::HEADER_HEIGHT - Config::Display::FOOTER_HEIGHT;
    
    // Actually clear the content area
    CoreS3.Display.fillRect(0, contentY, CoreS3.Display.width(), contentHeight, Config::Display::COLOR_BACKGROUND);
    
    drawHeader();
    drawFooter();

    if (settingsMenu.isOpen) {
        drawSettingsMenu();
    } else {
        drawMainDisplay();
        drawTemperature();
    }
}

void checkBattery() {
    int batteryLevel = CoreS3.Power.getBatteryLevel();
    bool isCharging = CoreS3.Power.isCharging();
    
    if (batteryLevel <= 10 && !isCharging) {
        if (!state.lowBatteryWarning) {
            updateStatus("Low Battery!", Config::Display::COLOR_WARNING);
            state.lowBatteryWarning = true;
        }
    } else {
        state.lowBatteryWarning = false;
    }
}

void updateStatus(const String& message, uint32_t color) {
    // Only update if message or color has changed
    if (message != state.statusMessage || color != state.statusColor) {
        state.statusMessage = message;
        state.statusColor = color;
        
        // Update status display area
        int statusY = CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT - Config::Display::STATUS_HEIGHT;
        
        // Draw status background
        CoreS3.Display.fillRect(Config::Display::MARGIN, statusY,
                              CoreS3.Display.width() - 2 * Config::Display::MARGIN,
                              Config::Display::STATUS_HEIGHT,
                              Config::Display::COLOR_SECONDARY);
                              
        // Draw status highlight
        CoreS3.Display.fillRect(Config::Display::MARGIN + 4, statusY + 4,
                              CoreS3.Display.width() - 2 * Config::Display::MARGIN - 8,
                              Config::Display::STATUS_HEIGHT - 8,
                              Config::Display::COLOR_BACKGROUND);
        
        // Draw status text
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextSize(1);
        CoreS3.Display.setTextColor(color);
        CoreS3.Display.drawString(message.c_str(), 
                                CoreS3.Display.width() / 2, 
                                statusY + Config::Display::STATUS_HEIGHT / 2);
    }
}

float readTemperature() {
    float rawTemp = ncir2.getTempValue();
    float tempC = rawTemp / 100.0;  // Convert to Celsius
    return tempC;
}

void playSound(bool success) {
    if (!settings.soundEnabled) return;
    
    CoreS3.Speaker.tone(success ? 1000 : 500, success ? 50 : 100);
    delay(success ? 50 : 100);
}

void drawHeader() {
    // Draw header background
    CoreS3.Display.fillRect(0, 0, CoreS3.Display.width(), Config::Display::HEADER_HEIGHT, Config::Display::COLOR_SECONDARY);
    
    // Draw title
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.setTextSize(1);  // Consistent text size
    CoreS3.Display.drawString("TerpMeter Pro", CoreS3.Display.width() / 2, Config::Display::HEADER_HEIGHT / 2);
    
    // Draw battery indicator
    int batteryLevel = CoreS3.Power.getBatteryLevel();
    bool isCharging = CoreS3.Power.isCharging();
    
    // Battery icon dimensions
    const int battW = 30;
    const int battH = 15;
    const int battX = CoreS3.Display.width() - battW - Config::Display::MARGIN;
    const int battY = (Config::Display::HEADER_HEIGHT - battH) / 2;
    
    // Draw battery outline
    CoreS3.Display.drawRect(battX, battY, battW, battH, Config::Display::COLOR_TEXT);
    CoreS3.Display.fillRect(battX + battW, battY + 4, 2, 7, Config::Display::COLOR_TEXT);
    
    // Fill battery based on level
    uint32_t battColor = batteryLevel > 50 ? Config::Display::COLOR_SUCCESS :
                        batteryLevel > 20 ? Config::Display::COLOR_WARNING :
                        Config::Display::COLOR_ERROR;
    
    if (isCharging) battColor = Config::Display::COLOR_SUCCESS;
    
    int fillW = (battW - 4) * batteryLevel / 100;
    CoreS3.Display.fillRect(battX + 2, battY + 2, fillW, battH - 4, battColor);
}

void drawFooter() {
    int footerY = CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT;
    
    // Draw footer background
    CoreS3.Display.fillRect(0, footerY, CoreS3.Display.width(), Config::Display::FOOTER_HEIGHT, Config::Display::COLOR_SECONDARY);
    
    // Create and draw buttons
    int buttonWidth = (CoreS3.Display.width() - 3 * Config::Display::MARGIN) / 2;
    
    // Only show Monitor button when not in settings
    if (!settingsMenu.isOpen) {
        Button monitorBtn(Config::Display::MARGIN, 
                        footerY + (Config::Display::FOOTER_HEIGHT - Config::Display::BUTTON_HEIGHT) / 2,
                        buttonWidth, 
                        Config::Display::BUTTON_HEIGHT - 4,
                        state.isMonitoring ? "Stop" : "Monitor",
                        true, true);
        monitorBtn.toggleState = state.isMonitoring;
        monitorBtn.draw();
    }
    
    // Settings/Back button
    Button settingsBtn(CoreS3.Display.width() - buttonWidth - Config::Display::MARGIN,
                      footerY + (Config::Display::FOOTER_HEIGHT - Config::Display::BUTTON_HEIGHT) / 2,
                      buttonWidth,
                      Config::Display::BUTTON_HEIGHT - 4,
                      settingsMenu.isOpen ? "Back" : "Settings");
    settingsBtn.draw();
}

void drawMainDisplay() {
    int contentY = Config::Display::HEADER_HEIGHT + Config::Display::MARGIN;
    int contentHeight = CoreS3.Display.height() - Config::Display::HEADER_HEIGHT - Config::Display::FOOTER_HEIGHT - 2 * Config::Display::MARGIN;
    
    // Draw boxes first
    CoreS3.Display.drawRoundRect(Config::Display::MARGIN, contentY, 
                               CoreS3.Display.width() - 2 * Config::Display::MARGIN, 
                               contentHeight, 8, Config::Display::COLOR_TEXT);
}

void drawTemperature() {
    // Don't draw if settings menu is open
    if (settingsMenu.isOpen) return;
    
    int contentY = Config::Display::HEADER_HEIGHT + Config::Display::MARGIN;
    int contentHeight = CoreS3.Display.height() - Config::Display::HEADER_HEIGHT - Config::Display::FOOTER_HEIGHT - 2 * Config::Display::MARGIN;
    
    // Calculate the temperature text area
    int tempTextY = contentY + contentHeight / 2 - 40;
    int tempTextHeight = 80;
    
    // Only clear the temperature text area
    CoreS3.Display.fillRect(
        Config::Display::MARGIN + 1,
        tempTextY,
        CoreS3.Display.width() - 2 * Config::Display::MARGIN - 2,
        tempTextHeight,
        Config::Display::COLOR_BACKGROUND
    );
    
    // Draw temperature
    float tempC = state.currentTemp;  // Already in Celsius
    float tempF = (tempC * 9.0 / 5.0) + 32.0;
    float displayTemp = settings.useCelsius ? tempC : tempF;
    
    // Format temperature string
    char tempStr[32];
    sprintf(tempStr, "%d%c", (int)round(displayTemp), settings.useCelsius ? 'C' : 'F');
    
    // Draw temperature with larger text
    CoreS3.Display.setTextSize(4);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.drawString(tempStr, CoreS3.Display.width() / 2, contentY + contentHeight / 2 - 20);
    
    // Draw emissivity below temperature
    char emisStr[32];
    sprintf(emisStr, "E=%.2f", settings.emissivity);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString(emisStr, CoreS3.Display.width() / 2, contentY + contentHeight / 2 + 30);
}

void drawSettingsMenu() {
    int menuY = Config::Display::HEADER_HEIGHT + Config::Display::MARGIN;
    int menuHeight = CoreS3.Display.height() - Config::Display::HEADER_HEIGHT - Config::Display::FOOTER_HEIGHT - 2 * Config::Display::MARGIN;
    int itemHeight = menuHeight / settingsMenu.numItems;
    
    // Draw menu background
    CoreS3.Display.fillRect(Config::Display::MARGIN, menuY, 
                          CoreS3.Display.width() - 2 * Config::Display::MARGIN, 
                          menuHeight, Config::Display::COLOR_SECONDARY);
    
    // Draw menu items
    CoreS3.Display.setTextDatum(middle_left);
    CoreS3.Display.setTextSize(1);
    
    for (int i = 0; i < settingsMenu.numItems; i++) {
        int itemY = menuY + i * itemHeight;
        bool isSelected = (i == settingsMenu.selectedItem);
        
        // Draw selection highlight
        if (isSelected) {
            CoreS3.Display.fillRect(Config::Display::MARGIN, itemY, 
                                  CoreS3.Display.width() - 2 * Config::Display::MARGIN, 
                                  itemHeight, Config::Display::COLOR_PRIMARY);
        }
        
        // Draw item text
        CoreS3.Display.setTextColor(isSelected ? Config::Display::COLOR_BACKGROUND : Config::Display::COLOR_TEXT);
        CoreS3.Display.drawString(settingsMenu.menuItems[i], 
                                Config::Display::MARGIN * 2, 
                                itemY + itemHeight / 2);
        
        // Draw current value
        String value;
        switch (i) {
            case 0: value = settings.useCelsius ? "Celsius" : "Fahrenheit"; break;
            case 1: value = settings.soundEnabled ? "On" : "Off"; break;
            case 2: value = String(settings.brightness); break;
            case 3: value = String(settings.emissivity, 2); break;
            case 4: value = String(settings.targetTemp, 1) + (settings.useCelsius ? "°C" : "°F"); break;
            case 5: value = String(settings.tempTolerance, 1) + (settings.useCelsius ? "°C" : "°F"); break;
        }
        
        CoreS3.Display.drawString(value, 
                                CoreS3.Display.width() - Config::Display::MARGIN * 3, 
                                itemY + itemHeight / 2);
    }
}

void handleSettingsTouch(int x, int y) {
    int menuY = Config::Display::HEADER_HEIGHT + Config::Display::MARGIN;
    int menuHeight = CoreS3.Display.height() - Config::Display::HEADER_HEIGHT - Config::Display::FOOTER_HEIGHT - 2 * Config::Display::MARGIN;
    int itemHeight = menuHeight / settingsMenu.numItems;
    
    // Check if touch is within menu area
    if (x >= Config::Display::MARGIN && 
        x <= CoreS3.Display.width() - Config::Display::MARGIN &&
        y >= menuY && y <= menuY + menuHeight) {
        
        // Calculate which item was touched
        int touchedItem = (y - menuY) / itemHeight;
        if (touchedItem >= 0 && touchedItem < settingsMenu.numItems) {
            settingsMenu.selectedItem = touchedItem;
            
            // Handle item selection
            switch (touchedItem) {
                case 0: // Temperature Unit
                    settings.useCelsius = !settings.useCelsius;
                    break;
                case 1: // Sound
                    settings.soundEnabled = !settings.soundEnabled;
                    if (settings.soundEnabled) playSound(true);
                    break;
                case 2: // Brightness
                    settings.brightness = (settings.brightness + 64) % 256;
                    CoreS3.Display.setBrightness(settings.brightness);
                    break;
                case 3: // Emissivity
                    adjustEmissivity();
                    break;
                case 4: // Target Temperature
                    settings.targetTemp += settings.useCelsius ? 5.0f : 10.0f;
                    if (settings.targetTemp > Config::Temperature::MAX_TEMP_C)
                        settings.targetTemp = Config::Temperature::MIN_TEMP_C;
                    break;
                case 5: // Tolerance
                    settings.tempTolerance += settings.useCelsius ? 0.5f : 1.0f;
                    if (settings.tempTolerance > 20.0f) settings.tempTolerance = 1.0f;
                    break;
            }
            
            settings.save();
            if (settings.soundEnabled) playSound(true);
            
            // Clear and redraw the settings menu
            int contentY = Config::Display::HEADER_HEIGHT;
            int contentHeight = CoreS3.Display.height() - Config::Display::HEADER_HEIGHT - Config::Display::FOOTER_HEIGHT;
            CoreS3.Display.fillRect(0, contentY, CoreS3.Display.width(), contentHeight, Config::Display::COLOR_BACKGROUND);
            drawSettingsMenu();
        }
    }
    // Check for back button touch
    if (y > CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT) {
        settingsMenu.isOpen = false;
        settings.save();  // Save settings before leaving
        // Force a complete screen refresh
        CoreS3.Display.fillRect(0, 0, CoreS3.Display.width(), CoreS3.Display.height(), Config::Display::COLOR_BACKGROUND);
        updateDisplay();  // This will now clear the screen completely
        return;
    }

    // Only process menu items if settings menu is open
    if (!settingsMenu.isOpen) return;
}

void adjustEmissivity() {
    // Define constants for emissivity limits and step
    const float EMISSIVITY_MIN = 0.65f;
    const float EMISSIVITY_MAX = 1.00f;
    const float EMISSIVITY_STEP = 0.01f;

    float originalEmissivity = settings.emissivity;
    float tempEmissivity = settings.emissivity;
    
    // Ensure starting value is within limits
    if (tempEmissivity < EMISSIVITY_MIN) tempEmissivity = EMISSIVITY_MIN;
    if (tempEmissivity > EMISSIVITY_MAX) tempEmissivity = EMISSIVITY_MAX;
    
    bool valueChanged = false;

    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);

    // Create buttons with landscape-optimized positions
    Button upBtn = {220, 60, 80, 60, "+", true, false};
    Button downBtn = {220, 140, 80, 60, "-", true, false};
    Button doneBtn = {10, 180, 100, 50, "Done", true, false};

    // Draw title
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Adjust Emissivity", 120, 30);

    // Create a larger value display box
    int valueBoxWidth = 160;
    int valueBoxHeight = 80;
    int valueBoxX = 20;  // Moved left
    int valueBoxY = 80;
    CoreS3.Display.drawRoundRect(valueBoxX, valueBoxY, valueBoxWidth, valueBoxHeight, 8, Config::Display::COLOR_TEXT);

    // Draw static buttons
    upBtn.draw();
    downBtn.draw();
    doneBtn.draw();

    bool adjusting = true;
    float lastDrawnValue = -1;  // Track last drawn value to prevent flicker

    while (adjusting) {
        // Only update display if value changed
        if (tempEmissivity != lastDrawnValue) {
            // Clear only the value display area
            CoreS3.Display.fillRect(valueBoxX + 5, valueBoxY + 5, valueBoxWidth - 10, valueBoxHeight - 10, Config::Display::COLOR_BACKGROUND);

            // Display current value
            char emisStr[16];
            sprintf(emisStr, "%.2f", tempEmissivity);
            CoreS3.Display.setTextSize(3);
            CoreS3.Display.drawString(emisStr, valueBoxX + (valueBoxWidth / 2), valueBoxY + valueBoxHeight / 2);
            
            lastDrawnValue = tempEmissivity;
        }

        CoreS3.update();
        if (CoreS3.Touch.getCount()) {
            auto touched = CoreS3.Touch.getDetail();
            if (touched.wasPressed()) {
                if (upBtn.contains(touched.x, touched.y)) {
                    if (tempEmissivity < EMISSIVITY_MAX) {
                        tempEmissivity += EMISSIVITY_STEP;
                        if (tempEmissivity > EMISSIVITY_MAX) tempEmissivity = EMISSIVITY_MAX;
                        valueChanged = (tempEmissivity != originalEmissivity);
                        playSound(true);
                    }
                } else if (downBtn.contains(touched.x, touched.y)) {
                    if (tempEmissivity > EMISSIVITY_MIN) {
                        tempEmissivity -= EMISSIVITY_STEP;
                        if (tempEmissivity < EMISSIVITY_MIN) tempEmissivity = EMISSIVITY_MIN;
                        valueChanged = (tempEmissivity != originalEmissivity);
                        playSound(true);
                    }
                } else if (doneBtn.contains(touched.x, touched.y)) {
                    adjusting = false;
                    playSound(true);
                }
                delay(10);  // Short debounce
            }
        }
    }

    // If emissivity was changed, show confirmation screen
    if (valueChanged) {
        CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
        CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.drawString("Emissivity Changed", CoreS3.Display.width() / 2, 40);
        CoreS3.Display.drawString("Restart Required", CoreS3.Display.width() / 2, 80);

        // Show old and new values
        char oldStr[32], newStr[32];
        sprintf(oldStr, "Old: %.2f", originalEmissivity);
        sprintf(newStr, "New: %.2f", tempEmissivity);
        CoreS3.Display.drawString(oldStr, CoreS3.Display.width() / 2, 120);
        CoreS3.Display.drawString(newStr, CoreS3.Display.width() / 2, 150);

        // Create confirm/cancel buttons - moved up
        Button confirmBtn = {10, 190, 145, 50, "Restart", true};
        Button cancelBtn = {165, 190, 145, 50, "Cancel", true};

        confirmBtn.draw(Config::Display::COLOR_SUCCESS);
        cancelBtn.draw(Config::Display::COLOR_ERROR);

        // Wait for user choice
        bool waiting = true;
        while (waiting) {
            CoreS3.update();
            if (CoreS3.Touch.getCount()) {
                auto touched = CoreS3.Touch.getDetail();
                if (touched.wasPressed()) {
                    if (confirmBtn.contains(touched.x, touched.y)) {
                        // Save new emissivity and restart
                        settings.emissivity = tempEmissivity;
                        settings.save();
                        ncir2.setEmissivity(settings.emissivity);
                        playSound(true);
                        delay(500);
                        ESP.restart();
                    } else if (cancelBtn.contains(touched.x, touched.y)) {
                        playSound(true);
                        waiting = false;
                    }
                }
            }
            delay(10);
        }
    }

    // Return to main interface
    drawMainDisplay();
    drawTemperature();
    updateDisplay();
}

void drawToggleSwitch(int x, int y, bool state) {
    const int width = 50;
    const int height = 24;
    const int knobSize = height - 4;
    
    // Draw background
    uint32_t bgColor = state ? Config::Display::COLOR_SUCCESS : Config::Display::COLOR_SECONDARY;
    CoreS3.Display.fillRoundRect(x, y, width, height, height / 2, bgColor);
    
    // Draw knob
    int knobX = state ? x + width - knobSize - 2 : x + 2;
    CoreS3.Display.fillCircle(knobX + knobSize / 2, y + height / 2, knobSize / 2, Config::Display::COLOR_TEXT);
}

void drawBatteryStatus() {
    // Battery indicator position in header
    int batteryX = CoreS3.Display.width() - 45;
    int batteryY = 5;
    
    // Get battery voltage and charging status
    float batteryVoltage = CoreS3.Power.getBatteryVoltage();
    bool isCharging = CoreS3.Power.isCharging();
    
    // Calculate battery percentage (approximate)
    float batteryPercentage = ((batteryVoltage - 3.3) / (4.2 - 3.3)) * 100;
    if (batteryPercentage > 100) batteryPercentage = 100;
    if (batteryPercentage < 0) batteryPercentage = 0;
    
    // Battery icon dimensions
    const int batteryWidth = 30;
    const int batteryHeight = 15;
    const int tipWidth = 3;
    const int tipHeight = 8;
    const int margin = 2;
    
    // Draw battery outline
    CoreS3.Display.drawRect(batteryX, batteryY, batteryWidth, batteryHeight, Config::Display::COLOR_TEXT);
    CoreS3.Display.fillRect(batteryX + batteryWidth, batteryY + (batteryHeight - tipHeight) / 2, 
                          tipWidth, tipHeight, Config::Display::COLOR_TEXT);
    
    // Draw battery level
    int fillWidth = ((batteryWidth - (2 * margin)) * batteryPercentage) / 100;
    uint32_t fillColor;
    
    if (isCharging) {
        fillColor = Config::Display::COLOR_SUCCESS;  // Green when charging
    } else if (batteryPercentage <= 20) {
        fillColor = Config::Display::COLOR_ERROR;    // Red when low
    } else if (batteryPercentage <= 50) {
        fillColor = Config::Display::COLOR_WARNING;  // Yellow when medium
    } else {
        fillColor = Config::Display::COLOR_SUCCESS;  // Green when high
    }
    
    if (fillWidth > 0) {
        CoreS3.Display.fillRect(batteryX + margin, batteryY + margin, 
                              fillWidth, batteryHeight - (2 * margin), fillColor);
    }
    
    // Draw charging indicator if applicable
    if (isCharging) {
        CoreS3.Display.fillTriangle(
            batteryX + batteryWidth / 2 - 3, batteryY + batteryHeight / 2,
            batteryX + batteryWidth / 2 + 3, batteryY + batteryHeight / 2,
            batteryX + batteryWidth / 2, batteryY + margin,
            Config::Display::COLOR_TEXT
        );
    }
}