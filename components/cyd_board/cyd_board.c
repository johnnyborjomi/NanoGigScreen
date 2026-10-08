#include "cyd_board.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "sdkconfig.h"

#ifdef CONFIG_CYD_PANEL_ILI9341
#include "esp_lcd_ili9341.h"
#endif

static const char *TAG = "cyd_board";

/* Disabled Kconfig bools are undefined, not 0. */
#ifdef CONFIG_CYD_PANEL_INVERT
#define PANEL_INVERT true
#else
#define PANEL_INVERT false
#endif
#ifdef CONFIG_CYD_ROTATE_180
#define ROTATE_180 true
#else
#define ROTATE_180 false
#endif
#ifdef CONFIG_CYD_TOUCH_MIRROR_X
#define TOUCH_MIRROR_X 1
#else
#define TOUCH_MIRROR_X 0
#endif
#ifdef CONFIG_CYD_TOUCH_MIRROR_Y
#define TOUCH_MIRROR_Y 1
#else
#define TOUCH_MIRROR_Y 0
#endif

#define LCD_HOST SPI2_HOST
#define TOUCH_HOST SPI3_HOST
#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
/* Two DMA buffers of this many lines (2 x 15 KB): 24, not 40, since Wi-Fi joined the image (its ~23 KB of
 * static RAM comes out of the heap the gig needs; 40 lines left ~9.6 KB free after boot). */
#define LVGL_BUFFER_LINES 24

/* Backlight PWM: the CYD's LED driver takes a plain GPIO; 5 kHz keeps it out of hearing and off camera. */
#define BL_TIMER LEDC_TIMER_0
#define BL_MODE LEDC_LOW_SPEED_MODE
#define BL_CHANNEL LEDC_CHANNEL_0
#define BL_RES LEDC_TIMER_10_BIT
#define BL_DUTY_MAX 1023
#define BL_FREQ_HZ 5000
/* Duty per level 1..10 in tenths of a percent: roughly even perceived steps, 1 still readable in the dark. */
static const uint16_t BL_LEVEL_PERMILLE[CYD_BRIGHTNESS_MAX] = { 30, 60, 100, 150, 220, 310, 420, 560, 750, 1000 };

static uint8_t s_bl_level = CYD_BRIGHTNESS_MAX;
static bool s_bl_on;
static bool s_rot180 = ROTATE_180;
static lv_display_t *s_disp;
static esp_lcd_panel_handle_t s_panel;

static void backlight_apply(void)
{
    uint32_t duty = 0;
    if (s_bl_on) {
        uint8_t lvl = s_bl_level < CYD_BRIGHTNESS_MIN ? CYD_BRIGHTNESS_MIN : s_bl_level > CYD_BRIGHTNESS_MAX ? CYD_BRIGHTNESS_MAX : s_bl_level;
        duty = (uint32_t)BL_LEVEL_PERMILLE[lvl - 1] * BL_DUTY_MAX / 1000;
    }
    ledc_set_duty(BL_MODE, BL_CHANNEL, duty);
    ledc_update_duty(BL_MODE, BL_CHANNEL);
}

void cyd_backlight(bool on)
{
    s_bl_on = on;
    backlight_apply();
}

void cyd_backlight_set_level(uint8_t level)
{
    if (level < CYD_BRIGHTNESS_MIN) level = CYD_BRIGHTNESS_MIN;
    if (level > CYD_BRIGHTNESS_MAX) level = CYD_BRIGHTNESS_MAX;
    s_bl_level = level;
    backlight_apply();
}

static esp_err_t init_backlight(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = BL_MODE,
        .duty_resolution = BL_RES,
        .timer_num = BL_TIMER,
        .freq_hz = BL_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    ledc_channel_config_t ch = {
        .gpio_num = CYD_LCD_BACKLIGHT,
        .speed_mode = BL_MODE,
        .channel = BL_CHANNEL,
        .timer_sel = BL_TIMER,
        .duty = 0, /* off until the first frame, so the boot does not flash garbage */
        .hpoint = 0,
    };
    return ledc_channel_config(&ch);
}

void cyd_led(bool r, bool g, bool b)
{
    gpio_set_level(CYD_LED_R, r ? 0 : 1);
    gpio_set_level(CYD_LED_G, g ? 0 : 1);
    gpio_set_level(CYD_LED_B, b ? 0 : 1);
}

static esp_err_t init_gpio(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << CYD_LED_R) | (1ULL << CYD_LED_G) | (1ULL << CYD_LED_B),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio");
    cyd_led(false, false, false);
    return init_backlight();
}

static esp_err_t init_panel(esp_lcd_panel_io_handle_t *io_out, esp_lcd_panel_handle_t *panel_out)
{
    spi_bus_config_t bus = {
        .mosi_io_num = CYD_LCD_MOSI,
        .miso_io_num = CYD_LCD_MISO,
        .sclk_io_num = CYD_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CYD_H_RES * LVGL_BUFFER_LINES * 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "lcd spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = CYD_LCD_DC,
        .cs_gpio_num = CYD_LCD_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, io_out), TAG, "lcd io");

    esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = CYD_LCD_RST,
        /* RGB, not BGR: with BGR the bench showed red and blue swapped (a red "1A" label came out
         * blue), even though the TFT_eSPI CYD2USB setup says TFT_BGR. The two stacks define the
         * bit differently; what matters is what the glass shows. */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
#ifdef CONFIG_CYD_PANEL_ILI9341
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(*io_out, &dev_cfg, panel_out), TAG, "ili9341");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(*io_out, &dev_cfg, panel_out), TAG, "st7789");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel_out), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel_out), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(*panel_out, PANEL_INVERT), TAG, "invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*panel_out, true), TAG, "disp on");
    return ESP_OK;
}

/*
 * Axis mapping + linear calibration, measured on the user's board with the firmware's own
 * touch log (2026-09-26): the controller's X channel runs DOWN the landscape screen and its Y
 * channel runs ACROSS it, and neither reaches the glass edge. After the driver's scaling to
 * x_max = 320 / y_max = 240 the corners read raw x 35..287 (top..bottom) and raw y 18..217
 * (left..right). Swap the axes here and stretch both onto the full 320x240.
 * (The Arduino XPT2046 test suggested unswapped axes; the firmware log wins.)
 */
#define TOUCH_RAW_ACROSS_MIN 18   /* driver y: left edge */
#define TOUCH_RAW_ACROSS_MAX 217  /* driver y: right edge */
#define TOUCH_RAW_DOWN_MIN 35     /* driver x: top edge */
#define TOUCH_RAW_DOWN_MAX 287    /* driver x: bottom edge */

/*
 * Touch filter, two stages, per press:
 * 1. Median of the last three samples per axis: drops single wild samples, above all the one or two
 *    the resistive glass gives as the finger lifts (pressure falling), which a slider took as a jump
 *    of 8-20 dB in the last 60 ms of a drag (board log 2026-10-08). Costs one sample (~30 ms) of lag.
 * 2. Jitter window: a resting finger still wobbles +-2 px; the reported point moves only once the
 *    median leaves a +-TOUCH_JITTER_PX window, then follows TOUCH_JITTER_PX behind (drags stay smooth).
 * The hook is not called while nothing touches, so a gap of TOUCH_NEW_PRESS_US starts a fresh press.
 */
#define TOUCH_JITTER_PX 2
#define TOUCH_NEW_PRESS_US 100000

static int32_t median3(int32_t a, int32_t b, int32_t c)
{
    if (a > b) { int32_t t = a; a = b; b = t; }
    if (b > c) b = c;
    return a > b ? a : b;
}

static int32_t touch_follow(int32_t stable, int32_t raw)
{
    int32_t d = raw - stable;
    if (d > TOUCH_JITTER_PX) return raw - TOUCH_JITTER_PX;
    if (d < -TOUCH_JITTER_PX) return raw + TOUCH_JITTER_PX;
    return stable;
}

static void touch_calibrate(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    (void)tp; (void)strength; (void)max_point_num;
    static int32_t s_fx, s_fy;
    static int32_t s_hx[2], s_hy[2]; /* the two samples before this one */
    static int64_t s_last_us;
    int64_t now = esp_timer_get_time();
    bool fresh = now - s_last_us > TOUCH_NEW_PRESS_US;
    s_last_us = now;
    for (uint8_t i = 0; i < *point_num; i++) {
        int32_t across = (int32_t)y[i];
        int32_t down = (int32_t)x[i];
        int32_t sx = (across - TOUCH_RAW_ACROSS_MIN) * CYD_H_RES / (TOUCH_RAW_ACROSS_MAX - TOUCH_RAW_ACROSS_MIN);
        int32_t sy = (down - TOUCH_RAW_DOWN_MIN) * CYD_V_RES / (TOUCH_RAW_DOWN_MAX - TOUCH_RAW_DOWN_MIN);
        sx = sx < 0 ? 0 : sx >= CYD_H_RES ? CYD_H_RES - 1 : sx;
        sy = sy < 0 ? 0 : sy >= CYD_V_RES ? CYD_V_RES - 1 : sy;
        if (s_rot180) { sx = CYD_H_RES - 1 - sx; sy = CYD_V_RES - 1 - sy; } /* the glass turned with the panel */
        if (i == 0) {
            if (fresh) {
                s_hx[0] = s_hx[1] = sx;
                s_hy[0] = s_hy[1] = sy;
                s_fx = sx;
                s_fy = sy;
            } else {
                int32_t mx = median3(s_hx[0], s_hx[1], sx), my = median3(s_hy[0], s_hy[1], sy);
                s_hx[0] = s_hx[1]; s_hx[1] = sx;
                s_hy[0] = s_hy[1]; s_hy[1] = sy;
                s_fx = touch_follow(s_fx, mx);
                s_fy = touch_follow(s_fy, my);
            }
            sx = s_fx;
            sy = s_fy;
        }
        x[i] = (uint16_t)sx;
        y[i] = (uint16_t)sy;
    }
}

static esp_err_t init_touch(esp_lcd_touch_handle_t *tp_out)
{
    spi_bus_config_t bus = {
        .mosi_io_num = CYD_TOUCH_MOSI,
        .miso_io_num = CYD_TOUCH_MISO,
        .sclk_io_num = CYD_TOUCH_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(TOUCH_HOST, &bus, SPI_DMA_DISABLED), TAG, "touch spi bus");
    esp_lcd_panel_io_spi_config_t io_cfg = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(CYD_TOUCH_CS);
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TOUCH_HOST, &io_cfg, &io), TAG, "touch io");
    esp_lcd_touch_config_t cfg = {
        /* Scaling only; the axis swap and calibration happen in touch_calibrate(). */
        .x_max = CYD_H_RES,
        .y_max = CYD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = CYD_TOUCH_IRQ,
        .levels = { .reset = 0, .interrupt = 0 },
        .process_coordinates = touch_calibrate,
        .flags = {
            .swap_xy = 0,
            .mirror_x = TOUCH_MIRROR_X,
            .mirror_y = TOUCH_MIRROR_Y,
        },
    };
    return esp_lcd_touch_new_spi_xpt2046(io, &cfg, tp_out);
}

lv_display_t *cyd_board_init(void)
{
    if (init_gpio() != ESP_OK) return NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_handle_t panel = NULL;
    if (init_panel(&io, &panel) != ESP_OK) return NULL;
    s_panel = panel;

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 4;
    port_cfg.task_stack = 8192;
    if (lvgl_port_init(&port_cfg) != ESP_OK) return NULL;

    /* Landscape: the panel is 240x320 native; swap_xy + mirror_x = TFT_eSPI rotation 1 on the CYD. */
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = CYD_H_RES * LVGL_BUFFER_LINES,
        .double_buffer = true,
        .hres = CYD_H_RES,
        .vres = CYD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = true,
            .mirror_x = !s_rot180,
            .mirror_y = s_rot180,
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true, /* SPI panels take big-endian RGB565 */
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (!disp) return NULL;
    s_disp = disp;

    esp_lcd_touch_handle_t tp = NULL;
    if (init_touch(&tp) == ESP_OK) {
        lvgl_port_touch_cfg_t touch_cfg = { .disp = disp, .handle = tp };
        if (!lvgl_port_add_touch(&touch_cfg)) ESP_LOGW(TAG, "touch not registered with LVGL");
    } else {
        ESP_LOGW(TAG, "touch init failed: running without touch");
    }
    return disp;
}

/* The LVGL port applies its rotation config once at init; a run-time change is the same MADCTL
 * flip on the panel (swap_xy stays), then everything on screen is redrawn into the new mapping. */
void cyd_display_set_rotation(bool rotate_180)
{
    s_rot180 = rotate_180;
    if (!s_panel) return; /* before init: cyd_board_init() picks it up */
    esp_lcd_panel_mirror(s_panel, !s_rot180, s_rot180);
    if (s_disp) {
        lv_obj_invalidate(lv_display_get_screen_active(s_disp));
        lv_obj_invalidate(lv_display_get_layer_top(s_disp));
        lv_obj_invalidate(lv_display_get_layer_sys(s_disp));
    }
}

bool cyd_display_rotation(void) { return s_rot180; }
