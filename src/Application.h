
#define TS_ENABLE_SSL
#include <Arduino.h>
#include <EEPROM.h>
#include <StreamUtils.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESPmDNS.h>
#include <ESPUI.h>
#include <Ticker.h>
#include <Button2.h>
#include <ESP32Servo.h>
#include <SimpleCLI.h>
#include <ThingSpeak.h>
#include <Wire.h>
#include <SPI.h>
#include <M5Display.h>
#include <AXP192.h>
#include <esp32-hal-log.h>
#include <secrets.h>
#include <ESP32SharpIR.h>

enum class MESSAGE : int {
    _DO_NOTHING,
    _SINGLE_CLICK,
    _DOUBLE_CLICK,
    _TRIPLE_CLICK,
    _LONG_CLICK,
    _START_EVENT,
    _ONCE_EVENT,
    _LIMITED_TIMER,
    _ENDLESS_TIMER,
    _CHECK_INIT_ANGLE,
    _CHECK_PUSH_ANGLE,
    _SAVE_SETTING,
};

class Application {
   public:
    Application() {
        _interval   = 0;
        _period     = 0;
        _init_angle = 0;
        _push_angle = 0;
    }

    //!LCD
    M5Display Lcd = M5Display();

    //!Power
    AXP192 Axp = AXP192();

    static void push_down_limited(void) {
        _message = MESSAGE::_LIMITED_TIMER;
    }

    static void push_down_endless(void) {
        _message = MESSAGE::_ENDLESS_TIMER;
    }

    static void handler(Button2& btn) {
        switch (btn.getClickType()) {
            case SINGLE_CLICK:
                _message = MESSAGE::_SINGLE_CLICK;
                break;
            case DOUBLE_CLICK:
                Serial.print("double ");
                _message = MESSAGE::_DOUBLE_CLICK;
                break;
            case TRIPLE_CLICK:
                Serial.print("triple ");
                _message = MESSAGE::_TRIPLE_CLICK;
                break;
            case LONG_CLICK:
                Serial.print("long");
                _message = MESSAGE::_LONG_CLICK;
                break;
        }
        Serial.print("click");
        Serial.print(" (");
        Serial.print(btn.getNumberOfClicks());
        Serial.println(")");
    }

    static void toolCallback(cmd* c) {
        Command cmd(c);

        Argument arg  = cmd.getArgument(0);
        String argVal = arg.getValue();

        if (argVal == "on") {
            Serial.println("Trigger on once.");
            _message = MESSAGE::_ONCE_EVENT;
        }
    }

    static void timerCallback(cmd* c) {
        Command cmd(c);

        Argument start = cmd.getArgument("start");
        Argument stop  = cmd.getArgument("stop");

        if (start.getName() == "start") {
            _set_count = start.getValue().toInt();

            if (_set_count > 0) {
                Serial.println("Timer on");
                _message = MESSAGE::_START_EVENT;
            }
        }

        if (stop.isSet()) {
            Serial.println("Timer off");
            _message = MESSAGE::_SINGLE_CLICK;
        }
    }

    // Callback in case of an error
    static void errorCallback(cmd_error* e) {
        CommandError cmdError(e);  // Create wrapper object

        Serial.print("ERROR: ");
        Serial.println(cmdError.toString());

        _message = MESSAGE::_SINGLE_CLICK;
    }

    static void inputPeriod(Control* sender, int value) {
        //log_d("Select: ID: %d Value: %s", sender->id, sender->value);
        _period = sender->value.toInt() * 1000;
    }

    static void inputInitAngle(Control* sender, int value) {
        //log_d("Select: ID: %d Value: %s", sender->id, sender->value);
        _init_angle = sender->value.toInt();
        _message    = MESSAGE::_CHECK_INIT_ANGLE;
    }

    static void inputPushDownAngle(Control* sender, int value) {
        //log_d("Select: ID: %d Value: %s", sender->id, sender->value);
        _push_angle = _init_angle + sender->value.toInt();
    }

    static void saveButton(Control* sender, int value) {
        //log_d("Select: ID: %d Value: %s", sender->id, sender->value);
        _message = MESSAGE::_SAVE_SETTING;
    }

    void initEEPROM(void) {
        EEPROM.begin(256 * 3);
    }

    void initESPUI(void) {
        ESPUI.setVerbosity(Verbosity::Quiet);

        uint16_t tabSetting = ESPUI.addControl(ControlType::Tab, "Push Down Settings", "Push Down Settings");
        uint16_t tabInfo    = ESPUI.addControl(ControlType::Tab, "Network Information", "Network Information");

        //Nwtwork Settings infomation
        ESPUI.addControl(ControlType::Label, "WiFi SSID", WiFi.SSID(), ControlColor::Sunflower, tabInfo);
        ESPUI.addControl(ControlType::Label, "ESP32 MAC Address", WiFi.macAddress(), ControlColor::Sunflower, tabInfo);
        ESPUI.addControl(ControlType::Label, "ESP32 IP Address", WiFi.localIP().toString(), ControlColor::Sunflower, tabInfo);

        //Push Down Settings
        _periodID    = ESPUI.number("Period", inputPeriod, ControlColor::Alizarin, _period, 1, 10);
        _initAngleID = ESPUI.slider("Init angle", inputInitAngle, ControlColor::Alizarin, _init_angle, 0, 180);
        _pushAngleID = ESPUI.slider("Push Down angle", inputPushDownAngle, ControlColor::Alizarin, _push_angle, 0, 180);
        ESPUI.addControl(ControlType::Button, "Save", "Save", ControlColor::Alizarin, tabSetting, saveButton);

        _inPeriod    = ESPUI.getControl(_periodID);
        _inInitAngle = ESPUI.getControl(_initAngleID);
        _inPushAngle = ESPUI.getControl(_pushAngleID);

        loadControlValue(256 * 0, _inPeriod);
        loadControlValue(256 * 1, _inInitAngle);
        loadControlValue(256 * 2, _inPushAngle);

        _inPeriod->parentControl    = tabSetting;
        _inInitAngle->parentControl = tabSetting;
        _inPushAngle->parentControl = tabSetting;

        ESPUI.begin("ESP32 Push Down");

        _interval   = _period / 2;
    }

    void saveControlValue(size_t address, Control* control) {
        if (!control) {
            return;
        }

        DynamicJsonDocument doc(256);
        EepromStream eeprom(address, 256);

        //doc["type"]  = (int)control->type;
        doc["value"] = control->value;
        //doc["id"]    = control->id;
        //doc["color"] = (int)control->color;

        serializeJson(doc, eeprom);
        eeprom.flush();
    }

    void loadControlValue(size_t address, Control* control) {
        if (!control) {
            return;
        }

        DynamicJsonDocument doc(256);
        EepromStream eeprom(address, 256);

        deserializeJson(doc, eeprom);

        //control->type  = doc["type"];
        control->value = (const char*)doc["value"];
        //control->id    = doc["id"];
        //control->color = doc["color"];
    }

    void initWiFi(void) {
        WiFi.setHostname(SECRET_HOSTNAME);

        // try to connect to existing network
        WiFi.begin(SECRET_SSID, SECRET_PASSWORD);
        Serial.print("\n\nTry to connect to existing network\n");

        uint8_t timeout = 10;

        // Wait for connection, 5s timeout
        do {
            delay(500);
            Serial.print(".");
            timeout--;
        } while (timeout && WiFi.status() != WL_CONNECTED);

        if (!MDNS.begin(SECRET_HOSTNAME)) {
            log_e("Error setting up MDNS responder!");
        } else {
            log_i("mDNS responder started");
        }
    }

    void initPSD(uint32_t psdPort) {
        pinMode(psdPort, INPUT);
        gpio_pulldown_dis(GPIO_NUM_25);
        gpio_pullup_dis(GPIO_NUM_25);

        _sensor = new ESP32SharpIR(ESP32SharpIR::GP2Y0A21YK0F, psdPort);

        // Setting the filter resolution to 0.1
        _sensor->setFilterRate(0.1f);
    }

    void initServo(uint32_t servoNum) {
        pinMode(servoNum, OUTPUT);
        _servo.attach(servoNum);
        _servo.write(_init_angle);
    }

    void initButton(uint32_t buttonNum) {
        //init button
        _button.setClickHandler(handler);
        _button.setLongClickHandler(handler);
        _button.setDoubleClickHandler(handler);
        _button.setTripleClickHandler(handler);

        _button.begin(buttonNum);
    }

    void initCLI(void) {
        //init CLI
        _cli.setOnError(errorCallback);
        _tool = _cli.addSingleArgCmd("tool", toolCallback);

        _continue = _cli.addCommand("timer", timerCallback);
        _continue.addArgument("start", "0");
        _continue.addFlagArgument("stop");

        Serial.println("Type: tool on, timer -start {count}, timer -stop");
        Serial.print("$ ");
    }

    void initM5StickCPlus(void) {
        Lcd.begin();
        Axp.begin();
        Axp.EnableCoulombcounter();  //Turn on 5V output

        Lcd.setRotation(0);  // Landscape (Portrait + 90)
        Lcd.setCursor(0, 0);
        Lcd.fillScreen(TFT_BLACK);
        Lcd.setTextSize(2);
        Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
        Lcd.printEfont("PowerON!");
    }

    void detection(void) {
        float dist = _sensor->getDistanceFloat();
        String value(dist);
        value.concat("cm ");
        Lcd.setCursor(0, 32);
        Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        Lcd.printEfont(value.c_str());

        if (dist < 11.0f) {
            _message = MESSAGE::_ONCE_EVENT;
        }
    }

    void servoOn(void) {
        _servo.write(_init_angle);  //OFF
        _servo.write(_push_angle);  //ON
        delay(_interval);           //interval
        _servo.write(_init_angle);  //OFF
    }

    void printCount(void) {
        Serial.printf("\x1b[31m%d\x1b[0m\n", _total_count + 1);
        Serial.print("$ ");
        _total_count++;
    }

    void begin(uint32_t servoPort, uint32_t buttonPort, uint32_t psdPort) {
        Serial.begin(115200);

        initM5StickCPlus();
        initWiFi();
        initEEPROM();
        initESPUI();
        initPSD(psdPort);
        initServo(servoPort);
        initButton(buttonPort);
        initCLI();
    }

    void update(void) {
        _button.loop();

        switch (_message) {
            case MESSAGE::_START_EVENT:
                log_printf("(LIMITED)Start.\n");
                log_d("Period = %d ms, init angle = %d degree, push angle = %d degree \n", _period, _init_angle, _push_angle);

                _timer.attach_ms(_period, push_down_limited);
                _message = MESSAGE::_DO_NOTHING;
                break;
            case MESSAGE::_LONG_CLICK:
                log_printf("(ENDLESS)Start.\n");
                log_d("Period = %d ms, init angle = %d degree, push angle = %d degree \n", _period, _init_angle, _push_angle);

                _timer.attach_ms(_period, push_down_endless);
                _message = MESSAGE::_DO_NOTHING;
                break;
            case MESSAGE::_LIMITED_TIMER:
                if (_total_count < _set_count) {
                    servoOn();
                    printCount();

                    _message = MESSAGE::_DO_NOTHING;
                } else {
                    log_printf("Setting number of times : %d\n", _set_count);
                    _message = MESSAGE::_SINGLE_CLICK;
                }
                break;
            case MESSAGE::_ONCE_EVENT:
                servoOn();
                printCount();
                delay(_period);

                _message = MESSAGE::_DO_NOTHING;
                break;
            case MESSAGE::_ENDLESS_TIMER:
                servoOn();
                printCount();

                _message = MESSAGE::_DO_NOTHING;
                break;
            case MESSAGE::_SINGLE_CLICK:
            case MESSAGE::_DOUBLE_CLICK:
            case MESSAGE::_TRIPLE_CLICK:
                _timer.detach();

                log_printf("Stop. Total Count: %d\n", _total_count);
                Serial.print("$ ");

                //init
                _set_count   = 0;
                _total_count = 0;

                _message = MESSAGE::_DO_NOTHING;
                break;
            case MESSAGE::_CHECK_INIT_ANGLE:
                _servo.write(_init_angle);
                _message = MESSAGE::_DO_NOTHING;
                break;
            case MESSAGE::_CHECK_PUSH_ANGLE:
                _servo.write(_push_angle);
                _message = MESSAGE::_DO_NOTHING;
                break;
            case MESSAGE::_SAVE_SETTING:
                saveControlValue(256 * 0, _inPeriod);
                saveControlValue(256 * 1, _inInitAngle);
                saveControlValue(250 * 2, _inPushAngle);
                break;
            default:
                //detection();
                break;
        }

        if (Serial.available()) {
            _cli.parse(Serial.readStringUntil('\n'));
            Serial.print("$ ");
        }
    }

   private:
    uint32_t _total_count;
    uint32_t _interval;
    static uint32_t _period;
    static uint32_t _init_angle;
    static uint32_t _push_angle;
    static uint32_t _set_count;
    static MESSAGE _message;
    Button2 _button;
    Servo _servo;
    Ticker _timer;
    SimpleCLI _cli;
    Command _tool;
    Command _continue;
    uint16_t _periodID;
    uint16_t _initAngleID;
    uint16_t _pushAngleID;

    ESP32SharpIR* _sensor;

    Control* _inPeriod;
    Control* _inInitAngle;
    Control* _inPushAngle;
};

MESSAGE Application::_message     = MESSAGE::_DO_NOTHING;
uint32_t Application::_set_count  = 0;
uint32_t Application::_init_angle = 0;
uint32_t Application::_push_angle = 0;
uint32_t Application::_period     = 0;
