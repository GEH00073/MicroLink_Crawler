#pragma once

class LGFX_ESP32_ST7789_SPI : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

  public:
    LGFX_ESP32_ST7789_SPI(int width, int height, int offset_y, int sclk, int mosi, int rst, int dc, int cs, int bl, int miso = -1, int busy = -1)
    {
      {
        auto cfg = _bus_instance.config();
        cfg.spi_host = SPI2_HOST;
        cfg.spi_mode = 3;
        cfg.spi_3wire = false;
        cfg.freq_write = 40000000;
        cfg.freq_read = 16000000;
        cfg.pin_sclk = sclk;
        cfg.pin_mosi = mosi;
        cfg.pin_miso = miso;
        cfg.pin_dc = dc;
        _bus_instance.config(cfg);
        _panel_instance.setBus(&_bus_instance);
      }

      {
        auto cfg = _panel_instance.config();
        cfg.pin_cs = cs;
        cfg.pin_rst = rst;
        cfg.pin_busy = busy;
        cfg.memory_width = 240;
        cfg.memory_height = height;
        cfg.panel_width = width;
        cfg.panel_height = height;
        cfg.offset_x = 0;
        cfg.offset_y = offset_y;
        cfg.invert = true;
        _panel_instance.config(cfg);
      }

      {
        auto cfg = _light_instance.config();
        cfg.pin_bl = bl;
        _light_instance.config(cfg);
        _panel_instance.setLight(&_light_instance);
      }

      setPanel(&_panel_instance);
    }
};
