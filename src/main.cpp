#include <Application.h>

#define ONOFF_PERIOD 1.0  //second
#define SERVO_PORT   3
#define BUTTON_PORT  7

static Application app;

//Settings
constexpr uint32_t period     = ONOFF_PERIOD * 1000;  //msec
constexpr uint32_t interval   = period / 2;           //msec
constexpr uint32_t init_angle = 75;                   //degree
constexpr uint32_t push_angle = init_angle + 50;      //degree

void setup(void) {
    app.setup(SERVO_PORT, BUTTON_PORT);
    app.begin(period, interval, init_angle, push_angle);
}

void loop(void) {
    app.update();
}
