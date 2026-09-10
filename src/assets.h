// 1. Frontend Web Assets (Gzip Compressed)
extern const uint8_t web_index_html[]  asm("_binary_index_html_start");
extern const uint8_t web_index_js[]    asm("_binary_index_js_gz_start");
extern const uint8_t web_style_css[]   asm("_binary_style_css_gz_start");

// 2. System Version Asset (Raw Text)
extern const uint8_t version[]         asm("_binary__version_start");

// 3. Application Data JSON Assets (Raw Text JSON)
extern const uint8_t config_json[]    asm("_binary_config_json_start");
extern const uint8_t timezones_json[] asm("_binary_timezones_json_start");
extern const uint8_t presets_json[]   asm("_binary_presets_json_start");

extern const size_t index_html_length       asm("index_html_length");
extern const size_t index_js_gz_length      asm("index_js_gz_length");
extern const size_t style_css_gz_length     asm("style_css_gz_length");
extern const size_t version_length          asm("_version_length");
extern const size_t config_json_length      asm("config_json_length");
extern const size_t timezones_json_length   asm("timezones_json_length");
extern const size_t presets_json_length     asm("presets_json_length");

#define INDEX_HTML_SIZE     ((size_t)index_html_length)
#define INDEX_JS_SIZE       ((size_t)index_js_gz_length)
#define STYLE_CSS_SIZE      ((size_t)style_css_gz_length)
#define VERSION_SIZE        ((size_t)version_length)
#define CONFIG_JSON_SIZE    ((size_t)config_json_length)
#define TIMEZONES_JSON_SIZE ((size_t)timezones_json_length)
#define PRESETS_JSON_SIZE   ((size_t)presets_json_length)
