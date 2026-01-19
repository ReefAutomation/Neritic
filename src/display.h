#pragma once
#define TFT_WIDTH  80
#define TFT_HEIGHT 160
#define TFT_SCL    12   // SCL (SCK/CLK)
#define TFT_SDA    14   // SDA (MOSI/DIN)
#define TFT_RES    27   // Reset
#define TFT_DC     26   // Data/Command
#define TFT_CS     33   // Chip Select
#define TFT_BLK    25   // Backlight
#define TFT_DRIVER ST7735S

void setup_display();
void display_status(const char* preset, bool power, const char* ip);
