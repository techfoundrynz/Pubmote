# ESP LCD ST7789

[![Component Registry](https://components.espressif.com/components/jbrilha/esp_lcd_st7789/badge.svg)](https://components.espressif.com/components/jbrilha/esp_lcd_st7789)

This component prvides an implementation of the ST7789 LCD driver using the `esp_lcd` component API.

| LCD controller | Communication interface | Component name  |                                    Link to datasheet                                    |
| :------------: | :---------------------: | :-------------: | :-------------------------------------------------------------------------------------: |
|    ST7789     |   SPI (half-duplex)\*   | esp_lcd_st7789 | [Specification](https://www.buydisplay.com/download/ic/ST7789.pdf) |

## Usage in a project

1. As an ESP-IDF component, via `idf.py add-dependency`:

```bash
idf.py add-dependency "jbrilha/esp_lcd_st7789^1.0.0"
```

2. Or including in it `idf_component.yml`

   - As a dependency:

   ```bash
   dependencies:
     jbrilha/esp_lcd_st7789: "^1.0.0"
   ```

   - As a direct repository

   ```bash
   dependencies:
     esp_lcd_st7789:
       git: https://github.com/jbrilha/esp_lcd_st7789.git
   ```

## \*Important note regarding SPI

This display driver uses half-duplex communication, so instead of the usual MOSI/MISO separation, it uses a single SDA line.

This means that SPI does not require a MISO pin, like below:

```c
void init_lcd_spi() {
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = LCD_CLK_PIN,
        .mosi_io_num = LCD_MOSI_PIN,
        .miso_io_num = -1, // set to -1
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0;

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO));
}
```

If you intend to use other devices that require MISO in the same SPI bus, the bus config should include it normally.

## Examples

An example is present in the `examples` directory, using LVGL
