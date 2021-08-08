
#define TS_ENABLE_SSL
#include <Arduino.h>
#include <Button2.h>
#include <ESP32Servo.h>
#include <SimpleCLI.h>
#include <Ticker.h>
#include <AutoConnect.h>
#include <ESPUI.h>
#include <ESPmDNS.h>
#include <ThingSpeak.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

enum class MESSAGE : int {
    _NOTHING_PUSH,
    _SINGLE_CLICK,
    _DOUBLE_CLICK,
    _TRIPLE_CLICK,
    _LONG_CLICK,
    _TIMER_CLICK,
};

class Application {
   public:
    Application() {
        _interval   = 0;
        _period     = 0;
        _init_angle = 0;
        _push_angle = 0;
    }

    static void push_button(void) {
        _message = MESSAGE::_TIMER_CLICK;
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
        Command cmd(c);  // Create wrapper object

        // Get first (and only) Argument
        Argument arg = cmd.getArgument(0);

        // Get value of argument
        String argVal = arg.getValue();

        if (argVal == "on") {
            Serial.println("Trigger on once.");
            _message = MESSAGE::_TIMER_CLICK;
        }
    }

    static void timerCallback(cmd* c) {
        Command cmd(c);  // Create wrapper object

        Argument start = cmd.getArgument("start");
        Argument stop  = cmd.getArgument("stop");

        if (start.getName() == "start") {
            _set_count = start.getValue().toInt();

            if (_set_count > 0) {
                Serial.println("Timer on");
                _message = MESSAGE::_LONG_CLICK;
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

        if (cmdError.hasCommand()) {
            Serial.print("Did you mean \"");
            Serial.print(cmdError.getCommand().toString());
            Serial.println("\"?");
        }
    }

    void setup(uint32_t servoNum, uint32_t buttonNum) {
        Serial.begin(115200);

        _servo.attach(servoNum);

        _button.setClickHandler(handler);
        _button.setLongClickHandler(handler);
        _button.setDoubleClickHandler(handler);
        _button.setTripleClickHandler(handler);

        _button.begin(buttonNum);

        _cli.setOnError(errorCallback);

        _tool = _cli.addSingleArgCmd("tool", toolCallback);

        _continue = _cli.addCommand("timer", timerCallback);
        _continue.addArgument("start", "0");
        _continue.addFlagArgument("stop");

        Serial.println("Type: tool on, timer -start {{count}}, timer -stop");
        Serial.print("$ ");
    }

    void begin(uint32_t period, uint32_t interval, uint32_t init_angle, uint32_t push_angle) {
        _period     = period;
        _interval   = interval;
        _init_angle = init_angle;
        _push_angle = push_angle;
    }

    void update(void) {
        switch (_message) {
            case MESSAGE::_LONG_CLICK:  //サーボスタート
                Serial.println("Starting Servo.");
                _timer.attach_ms(_period, push_button);
                _message = MESSAGE::_NOTHING_PUSH;
                break;
            case MESSAGE::_SINGLE_CLICK:
            case MESSAGE::_DOUBLE_CLICK:
            case MESSAGE::_TRIPLE_CLICK:
                _timer.detach();

                Serial.print("Total Count: ");
                Serial.println(_total_count);
                Serial.println("Stopping Servo.");
                Serial.print("$ ");

                _set_count   = 0;
                _total_count = 0;
                _message     = MESSAGE::_NOTHING_PUSH;
                break;
            case MESSAGE::_TIMER_CLICK:
                _servo.write(_push_angle);  //ON
                delay(_interval);           //ON Time
                _servo.write(_init_angle);  //OFF
                _total_count++;

                Serial.print("\x1b[31m");
                Serial.print(_total_count);
                Serial.println("\x1b[0m");
                Serial.print("$ ");

                if (_total_count == _set_count) {
                    Serial.print("Setting number of times reached now. : ");
                    Serial.println(_set_count);
                    _message = MESSAGE::_SINGLE_CLICK;
                } else {
                    _message = MESSAGE::_NOTHING_PUSH;
                }
                break;
            default:
                _message = MESSAGE::_NOTHING_PUSH;
                _button.loop();
        }

        if (Serial.available()) {
            // Read out string from the serial monitor
            String input = Serial.readStringUntil('\n');
            // Parse the user input into the CLI
            _cli.parse(input);
            Serial.print("$ ");
        }

        if (_cli.errored()) {
            CommandError cmdError = _cli.getError();

            Serial.print("ERROR: ");
            Serial.println(cmdError.toString());

            if (cmdError.hasCommand()) {
                Serial.print("Did you mean \"");
                Serial.print(cmdError.getCommand().toString());
                Serial.println("\"?");
            }
            Serial.print("$ ");
        }

        yield();
    }

   private:
    uint32_t _total_count;
    uint32_t _interval;
    uint32_t _period;
    uint32_t _init_angle;
    uint32_t _push_angle;

    //for callback
    static uint32_t _set_count;
    static MESSAGE _message;

    Button2 _button;
    Servo _servo;
    Ticker _timer;

    SimpleCLI _cli;
    Command _tool;
    Command _continue;
};

uint32_t Application::_set_count = 0;
MESSAGE Application::_message    = MESSAGE::_NOTHING_PUSH;
