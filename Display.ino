enum MenuState { MAIN_MENU, THRESHOLD_MENU };
enum DetectionMode { MAGNITUDE, STA_LTA };

// Menu variables
MenuState currentMenu = MAIN_MENU;
DetectionMode currentMode = MAGNITUDE;

float magnitudeThreshold = 1.5;
float ratioThreshold = 3.0;

int cursorPosition = 0; // 0 = first item, 1 = second item

// Button pins
#define BUTTON_LEFT 12
#define BUTTON_MID  13
#define BUTTON_RIGHT 14

// Previous button states for debouncing
bool prevLeft = HIGH, prevMid = HIGH, prevRight = HIGH;

void drawMain_Menu() {
    gfx->fillScreen(0x0000); // clear screen

    // Wifi Indicator
    gfx->drawBitmap(117, 57, image_Wifi_Indicator_bits, 9, 5, 0xFFFF);

    gfx->setTextColor(0xFFFF);
    gfx->setTextWrap(false);

    // Title
    gfx->setCursor(2, 1);
    gfx->println("Menu");

    // Separator lines
    gfx->drawLine(0, 10, 127, 10, 0xFFFF);
    gfx->drawLine(0, 54, 127, 54, 0xFFFF);

    // Brand
    gfx->setCursor(2, 56);
    gfx->println("SeismoSense");

    // Cursor
    gfx->drawBitmap(2, 14 + cursorPosition * 12, image_Select_Cursor_bits, 4, 7, 0xFFFF);

    // Menu items
    gfx->setCursor(8, 14);
    gfx->println("Sleep");

    gfx->setCursor(8, 26);
    gfx->println("Threshold");
}

void drawThreshold_Menu() {
    gfx->fillScreen(0x0000);

    // Wifi Indicator
    gfx->drawBitmap(117, 57, image_Wifi_Indicator_bits, 9, 5, 0xFFFF);

    gfx->setTextColor(0xFFFF);
    gfx->setTextWrap(false);

    // Title
    gfx->setCursor(2, 2);
    gfx->println("Threshold");

    // Line separator
    gfx->drawLine(0, 10, 127, 10, 0xFFFF);
    gfx->drawLine(0, 54, 127, 54, 0xFFFF);

    // Brand
    gfx->setCursor(2, 56);
    gfx->println("SeismoSense");

    // Cursor
    gfx->drawBitmap(2, 14 + cursorPosition * 12, image_Select_Cursor_bits, 4, 7, 0xFFFF);

    // Menu items
    gfx->setCursor(8, 14);
    gfx->println("Mode 1 (Mag. Based)");

    gfx->setCursor(8, 26);
    gfx->println("Threshold Val.");

    gfx->setCursor(27, 34);
    if (currentMode == MAGNITUDE) {
        gfx->println(magnitudeThreshold, 1);
    } else {
        gfx->println(ratioThreshold, 1);
    }

    // Draw the bar graphics
    gfx->drawBitmap(23, 42, image_Layer_10_bits, 26, 1, 0xFFFF);
    gfx->drawBitmap(16, 35, image_Layer_11_bits, 5, 6, 0xFFFF);
    gfx->drawBitmap(51, 35, image_Layer_11_copy_1_bits, 5, 6, 0xFFFF);
}

// ---------------- Button Handling ----------------
void handleButtons() {
    bool left = digitalRead(BUTTON_LEFT);
    bool mid  = digitalRead(BUTTON_MID);
    bool right = digitalRead(BUTTON_RIGHT);

    // Navigate up
    if (left == LOW && prevLeft == HIGH) {
        cursorPosition = max(cursorPosition - 1, 0);
    }

    // Navigate down
    if (right == LOW && prevRight == HIGH) {
        cursorPosition = min(cursorPosition + 1, 1);
    }

    // Select / Return
    if (mid == LOW && prevMid == HIGH) {
        if (currentMenu == MAIN_MENU) {
            if (cursorPosition == 1) {
                currentMenu = THRESHOLD_MENU;
                cursorPosition = 0;
            }
        } else if (currentMenu == THRESHOLD_MENU) {
            if (cursorPosition == 0) {
                // Toggle Mode
                currentMode = (currentMode == MAGNITUDE) ? STA_LTA : MAGNITUDE;
            } else if (cursorPosition == 1) {
                // Increase threshold
                if (currentMode == MAGNITUDE) magnitudeThreshold += 0.1;
                else ratioThreshold += 0.1;
            }
        }
    }

    prevLeft = left;
    prevMid = mid;
    prevRight = right;
}

void loop() {
    handleButtons();

    if (currentMenu == MAIN_MENU) drawMain_Menu();
    else if (currentMenu == THRESHOLD_MENU) drawThreshold_Menu();
}

