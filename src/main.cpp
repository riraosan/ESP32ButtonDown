#include <Application.h>

#define SERVO_PORT  26
#define BUTTON_PORT 37
#define PSD_PORT    36

static Application app;

void setup(void) {
    app.begin(SERVO_PORT, BUTTON_PORT, PSD_PORT);
}

void loop(void) {
    app.update();
}
