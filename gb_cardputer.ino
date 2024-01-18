#define ENABLE_SOUND 0
#define ENABLE_LCD 1

#include "M5Cardputer.h"
#include "peanutgb/peanut_gb.h"
#include "SD.h"

#define ROM_FILE "/rom.gb"

SPIClass SPI2;

uint32_t swap_fb[144][160];

void debugPrint(const char* str) {
  M5Cardputer.Display.clearDisplay();
  M5Cardputer.Display.drawString(str, 0, 0);
}

struct priv_t
{
	/* Pointer to allocated memory holding GB file. */
	uint8_t *rom;
	/* Pointer to allocated memory holding save file. */
	uint8_t *cart_ram;

	/* Frame buffer */
	uint32_t fb[LCD_HEIGHT][LCD_WIDTH];
};

/**
 * Returns a byte from the ROM file at the given address.
 */
uint8_t gb_rom_read(struct gb_s *gb, const uint_fast32_t addr)
{
	const struct priv_t * const p = (const struct priv_t *)gb->direct.priv;
	return p->rom[addr];
}

/**
 * Returns a byte from the cartridge RAM at the given address.
 */
uint8_t gb_cart_ram_read(struct gb_s *gb, const uint_fast32_t addr)
{
	const struct priv_t * const p = (const struct priv_t *)gb->direct.priv;
	return p->cart_ram[addr];
}

/**
 * Writes a given byte to the cartridge RAM at the given address.
 */
void gb_cart_ram_write(struct gb_s *gb, const uint_fast32_t addr,
		       const uint8_t val)
{
	const struct priv_t * const p = (const struct priv_t *)gb->direct.priv;
	p->cart_ram[addr] = val;
}

/**
 * Returns a pointer to the allocated space containing the ROM. Must be freed.
 */
uint8_t *read_rom_to_ram(const char *file_name) {
  File rom_file = SD.open(file_name);
  size_t rom_size;
  char *readRom = NULL;

  rom_size = rom_file.size();

  if(rom_size == NULL)
    return NULL;

  readRom = (char*)malloc(rom_size);

  if(rom_file.readBytes(readRom, rom_size) != rom_size) {
    free(readRom);
    rom_file.close();
    return NULL;
  }

  uint8_t *rom = (uint8_t*)readRom;
  char debugRomStr[100];
  sprintf(debugRomStr, "f: %02x | l: %02x", rom[0], rom[rom_size-1]);
  debugPrint(debugRomStr);
  rom_file.close();
  return rom;
}

/**
 * Ignore all errors.
 */
void gb_error(struct gb_s *gb, const enum gb_error_e gb_err, const uint16_t val) {
  const char* gb_err_str[GB_INVALID_MAX] = {
    "UNKNOWN",
    "INVALID OPCODE",
    "INVALID READ",
    "INVALID WRITE",
    "HATL FOREVER"
  };

	struct priv_t * priv = (struct priv_t *)gb->direct.priv;

  free(priv->cart_ram);
  free(priv->rom);
}

#if ENABLE_LCD
/**
 * Draws scanline into framebuffer.
 */
void lcd_draw_line(struct gb_s *gb, const uint8_t pixels[160],
		   const uint_fast8_t line)
{
	struct priv_t *priv = (priv_t*)gb->direct.priv;
	const uint32_t palette[] = { 0xFFFFFF, 0xA5A5A5, 0x525252, 0x000000 };

	for(unsigned int x = 0; x < LCD_WIDTH; x++)
		priv->fb[line][x] = palette[pixels[x] & 3];
}

void draw_frame(uint32_t fb[144][160]) {
  //M5Cardputer.Display.clearDisplay();
  for(unsigned int i = 0; i < LCD_WIDTH; i++) {
    for(unsigned int j = 0; j < LCD_HEIGHT; j++) {
      if(fb[j][i] != swap_fb[j][i]) {
        M5Cardputer.Display.drawPixel((int32_t)i, (int32_t)j, fb[j][i]);
      }
      swap_fb[j][i] = fb[j][i];
    } 
  }
}

#endif

void setup() {
  // put your setup code here, to run once:
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);

  M5Cardputer.Display.setRotation(1);
  int textsize = M5Cardputer.Display.height() / 256;
  if(textsize == 0) {
    textsize = 1;
  }
  M5Cardputer.Display.setTextSize(textsize);

  debugPrint("Waiting for SD Card to Init...");
  SPI2.begin(
      M5.getPin(m5::pin_name_t::sd_spi_sclk),
      M5.getPin(m5::pin_name_t::sd_spi_miso),
      M5.getPin(m5::pin_name_t::sd_spi_mosi),
      M5.getPin(m5::pin_name_t::sd_spi_ss));
  while (false == SD.begin(M5.getPin(m5::pin_name_t::sd_spi_ss), SPI2)) {
    delay(1);
  }
  debugPrint("setTextSize");

  static struct gb_s gb;
  static struct priv_t priv;
  enum gb_init_error_e ret;
  debugPrint("postInit");

  if((priv.rom = read_rom_to_ram(ROM_FILE)) == NULL) {
    // error reporting
    debugPrint("Error at read_rom_to_ram!!");
  }

  // Initialize emulation context
  ret = gb_init(&gb, &gb_rom_read, &gb_cart_ram_read, &gb_cart_ram_write, &gb_error, &priv);

  if(ret != GB_INIT_NO_ERROR) {
    // error reporting
    debugPrint("GB_INIT error!!!");
  }

  priv.cart_ram = (uint8_t*)malloc(gb_get_save_size(&gb));

#if ENABLE_LCD
  gb_init_lcd(&gb, &lcd_draw_line);
  gb.direct.interlace = 0;
#endif

  debugPrint("Before loop");

  M5Cardputer.Display.clearDisplay();
  while(1) {
    const double target_speed_us = 1000000.0 / VERTICAL_SYNC;
    int_fast16_t delay;
    unsigned long start, end;
    struct timeval timecheck;

    gettimeofday(&timecheck, NULL);
    start = (long)timecheck.tv_sec * 1000000 +
      (long)timecheck.tv_usec;

    // reset joypad
    gb.direct.joypad = 0xff;

    M5Cardputer.update();
    if(M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
      for(auto i : status.word) {
        switch(i) {
          case 'e':
            gb.direct.joypad_bits.up = 0;
            break;
          case 'a':
            gb.direct.joypad_bits.left = 0;
            break;
          case 's':
            gb.direct.joypad_bits.down = 0;
            break;
          case 'd':
            gb.direct.joypad_bits.right = 0;
            break;
          case 'k':
            gb.direct.joypad_bits.b = 0;
            break;
          case 'l':
            gb.direct.joypad_bits.a = 0;
            break;
          case '1':
            gb.direct.joypad_bits.start = 0;
            break;
          case '2':
            gb.direct.joypad_bits.select = 0;
            break;
          default:
            break;
        }
      }
    }

    /* Execute CPU cycles until the screen has to be redrawn. */
    gb_run_frame(&gb);

    // draw here.
    draw_frame(priv.fb);

    gettimeofday(&timecheck, NULL);
    end = (long)timecheck.tv_sec * 1000000 +
            (long)timecheck.tv_usec;

    delay = target_speed_us - (end - start);

    /* If it took more than the maximum allowed time to draw frame,
    * do not delay.
    * Interlaced mode could be enabled here to help speed up
    * drawing.
    */
    if(delay < 0)
      continue;

    usleep(delay);
  }
}

void loop() {

}
