#include <TFT_eSPI.h>
#include "config.h"
#include "transition.h"

TFT_eSPI tft = TFT_eSPI(TFT_WIDTH, TFT_HEIGHT);

void setup_display() {
    tft.init();
    tft.setRotation(3); // Landscape, adjust as needed
    tft.fillScreen(TFT_BLACK);

    // No border for landscape (rotation 3)
    tft.fillScreen(TFT_BLACK);
    delay(400);
    // Draw a simple fish logo (centered for landscape)
    int logo_r = 12;
    int logo_cx = TFT_HEIGHT / 2;
    int logo_cy = 18;
    tft.fillCircle(logo_cx, logo_cy, logo_r, TFT_CYAN); // Fish body
    tft.fillTriangle(logo_cx + logo_r, logo_cy, logo_cx + logo_r + 8, logo_cy - 5, logo_cx + logo_r + 8, logo_cy + 5, TFT_CYAN); // Tail
    tft.fillCircle(logo_cx + 6, logo_cy - 3, 2, TFT_YELLOW); // Highlight
    tft.fillCircle(logo_cx - 7, logo_cy - 2, 2, TFT_BLACK); // Eye
    tft.drawPixel(logo_cx - 9, logo_cy - 2, TFT_WHITE); // Eye sparkle
    delay(700);
    // Clean and show the rest, all within 80px height
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    int w = tft.textWidth("DeepGlow");
    tft.setCursor((TFT_HEIGHT - w) / 2, 5);
    tft.println("DeepGlow");
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(1);
    w = tft.textWidth("Aquarium LED Controller");
    tft.setCursor((TFT_HEIGHT - w) / 2, 28);
    tft.println("Aquarium LED Controller");
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    w = tft.textWidth("by kabroxiko");
    tft.setCursor((TFT_HEIGHT - w) / 2, 40);
    tft.println("by kabroxiko");
    // Loading bar at the bottom (y=70)
    for (int i = 10; i < TFT_HEIGHT - 10; ++i) {
        tft.drawPixel(i, 70, TFT_BLUE);
        delay(2);
    }
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    w = tft.textWidth("Loading...");
    tft.setCursor((TFT_HEIGHT - w) / 2, 60);
    tft.println("Loading...");
    delay(500);
}

void display_status(const char* preset, bool power, const char* ip) {
    extern TransitionEngine transition;
    uint8_t targetBrightness = transition.getTargetBrightness();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(0, 0);
    tft.printf("Preset: %s\n", preset);
    tft.printf("Power: %s\n", power ? "ON" : "OFF");
    tft.printf("Bri: %3d%%\n", hexToPercent(targetBrightness));
    tft.printf("IP: %s\n", ip);
}
