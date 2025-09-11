#include "powerSave.h"
#include <interface.h>

#include <Keyboard.h>
#include <Adafruit_TCA8418.h>
#include <Wire.h>

Keyboard_Class Keyboard;

// TCA8418 for Cardputer ADV variant detection
Adafruit_TCA8418 tca;
bool isCardputerADV = false;

// TCA8418 configuration for ADV variant
#define TCA8418_I2C_ADDR 0x34
#define TCA8418_SDA_PIN 8
#define TCA8418_SCL_PIN 9

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    //    Keyboard.begin();
    pinMode(0, INPUT);
    pinMode(10, INPUT); // Pin that reads the
    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH); // Set GPIO5 HIGH for SD card compatibility (needed for ADV)
}

void _post_setup_gpio() { 
    // Try to detect Cardputer ADV by looking for TCA8418
    Serial.println("DEBUG: Detecting Cardputer hardware variant...");
    
    // Initialize I2C for ADV detection
    Wire.begin(TCA8418_SDA_PIN, TCA8418_SCL_PIN);
    delay(100);
    
    // Try to initialize TCA8418
    if (tca.begin(TCA8418_I2C_ADDR, &Wire)) {
        Serial.println("DEBUG: TCA8418 found - Cardputer ADV detected!");
        isCardputerADV = true;
        
        // Configure TCA8418 matrix
        tca.matrix(7, 8);
        Serial.println("DEBUG: TCA8418 configured for polling mode");
    } else {
        Serial.println("DEBUG: TCA8418 not found - Original Cardputer detected");
        isCardputerADV = false;
        
        // Initialize original keyboard for standard Cardputer
        Keyboard.begin();
    }
}
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <soc/adc_channel.h>
#include <soc/soc_caps.h>
/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    uint8_t percent;
    uint8_t _batAdcCh = ADC1_GPIO10_CHANNEL;
    uint8_t _batAdcUnit = 1;

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten((adc1_channel_t)_batAdcCh, ADC_ATTEN_DB_12);
    static esp_adc_cal_characteristics_t *adc_chars = nullptr;
    static constexpr int BASE_VOLATAGE = 3600;
    adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(
        (adc_unit_t)_batAdcUnit, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, BASE_VOLATAGE, adc_chars
    );
    int raw;
    raw = adc1_get_raw((adc1_channel_t)_batAdcCh);
    uint32_t volt = esp_adc_cal_raw_to_voltage(raw, adc_chars);

    float mv = volt * 2;
    percent = (mv - 3300) * 100 / (float)(4150 - 3350);

    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    if (brightval == 0) {
        analogWrite(TFT_BL, brightval);
    } else {
        int bl = MINBRIGHT + round(((255 - MINBRIGHT) * brightval / 100));
        analogWrite(TFT_BL, bl);
    }
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
** Auto-detects and supports both original Cardputer and Cardputer ADV
**********************************************************************/
void InputHandler(void) {
    static long tmp = 0;
    static unsigned long lastTCACheck = 0;
    
    // Handle TCA8418 keyboard (Cardputer ADV)
    if (isCardputerADV) {
        // Poll TCA8418 every 100ms for key events
        if (millis() - lastTCACheck > 100) {
            lastTCACheck = millis();
            
            // Check if there are any events available
            if (tca.available()) {
                while (tca.available()) {
                    int keycode = tca.getEvent();
                    if (keycode > 0) {
                        // Extract row and column from keycode
                        int row = (keycode & 0x70) >> 4; // Bits 4-6 for row
                        int col = keycode & 0x0F;        // Bits 0-3 for column
                        bool pressed = !(keycode & 0x80); // Bit 7 for press/release
                        
                        Serial.printf("TCA8418 Key Event: Row=%d, Col=%d, %s (keycode=0x%02X)\n", 
                                      row, col, pressed ? "PRESSED" : "RELEASED", keycode);
                        
                        // Map navigation keys for Cardputer ADV
                        if (pressed) {
                            if (row == 4 && col == 3) { // Enter key
                                SelPress = true;
                                Serial.println("MAPPED: Enter/Select pressed");
                            } else if (row == 3 && col == 9 || row == 3 && col == 6) { // Up arrow OR Left arrow
                                PrevPress = true;
                                Serial.println("MAPPED: Up/Left/Previous pressed");
                            } else if (row == 3 && col == 10 || row == 4 && col == 0) { // Down arrow OR Right arrow
                                NextPress = true;
                                Serial.println("MAPPED: Down/Right/Next pressed");
                            } else if (row == 0 && col == 1) { // Escape key
                                EscPress = true;
                                Serial.println("MAPPED: Escape pressed");
                            }
                            
                            AnyKeyPress = true;
                            wakeUpScreen(); // Reset power save timer on key press
                        }
                    }
                }
            }
        }
    } else {
        // Handle original Cardputer keyboard
        Keyboard.update();
    }
    
    // Common GPIO0 button and original keyboard handling
    if (millis() - tmp > 200 || LongPress) {
        bool buttonPressed = digitalRead(0) == LOW;
        bool keyboardPressed = !isCardputerADV && Keyboard.isPressed();
        
        if (buttonPressed || keyboardPressed) {
            tmp = millis();
            bool screenWasOff = wakeUpScreen();
            if (!screenWasOff) yield();
            
            // Handle GPIO0 button (works on both variants)
            if (buttonPressed) {
                SelPress = true;
                AnyKeyPress = true;
                wakeUpScreen();
            }
            
            // Handle original keyboard (only for original Cardputer)
            if (!isCardputerADV && keyboardPressed) {
                keyStroke key;
                Keyboard_Class::KeysState status = Keyboard.keysState();
                for (auto i : status.hid_keys) key.hid_keys.push_back(i);
                for (auto i : status.word) {
                    key.word.push_back(i);
                    if (i == '`') key.exit_key = true; // key pressed to try to exit
                }
                for (auto i : status.modifier_keys) key.modifier_keys.push_back(i);
                if (status.del) key.del = true;
                if (status.enter) key.enter = true;
                if (status.fn) key.fn = true;
                key.pressed = true;
                KeyStroke = key;
                if (Keyboard.isKeyPressed(',') || Keyboard.isKeyPressed(';')) PrevPress = true;
                if (Keyboard.isKeyPressed('`') || Keyboard.isKeyPressed(KEY_BACKSPACE)) EscPress = true;
                if (Keyboard.isKeyPressed('/') || Keyboard.isKeyPressed('.')) NextPress = true;
                if (Keyboard.isKeyPressed(KEY_ENTER)) SelPress = true;
                
                if (KeyStroke.pressed) {
                    String keyStr = "";
                    for (auto i : KeyStroke.word) {
                        if (keyStr != "") {
                            keyStr = keyStr + "+" + i;
                        } else {
                            keyStr += i;
                        }
                    }
                    // Serial.println(keyStr);
                }
            }
        } else if (!isCardputerADV) {
            // Only clear KeyStroke for original Cardputer
            KeyStroke.Clear();
            LongPressTmp = false;
        }
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to tornoff the device (name is odd btw)
**********************************************************************/
void checkReboot() {}
