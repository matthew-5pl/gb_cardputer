// Sound isn't done yet
// (or might never be)
#define ENABLE_SOUND 0
#define ENABLE_LCD 1

#define MAX_FILES 256

#include "M5Cardputer.h"
#include "peanutgb/peanut_gb.h"
#include "SD.h"

#define DEST_W 240
#define DEST_H 135

#define DEBUG_DELAY 0

#define DISPLAY_CENTER(x) x + (DEST_W/2 - LCD_WIDTH/2)

// SD card SPI class.
SPIClass SPI2;

// Second framebuffer to check for changed pixels.
uint32_t swap_fb[LCD_HEIGHT][LCD_WIDTH];

// Prints debug info to the display.
void debugPrint(const char* str) {
  M5Cardputer.Display.clearDisplay();
  M5Cardputer.Display.drawString(str, 0, 0);
#if DEBUG_DELAY
  delay(500);
#endif
}

// Penaut-GB structures and functions.
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
  // Open file from the SD card.
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
  // Print first and last byte of the ROM for debugging purposes.
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

// Draw a frame to the display while scaling it to fit.
// This is needed as the Cardputer's display has a height of 135px,
// while the GameBoy's has a height of 144px.
void fit_frame(uint32_t fb[144][160]) {
  //M5Cardputer.Display.clearDisplay();
  for(unsigned int i = 0; i < LCD_WIDTH; i++) {
    for(unsigned int j = 0; j < LCD_HEIGHT; j++) {
      if(fb[j * LCD_HEIGHT / DEST_H][i] != swap_fb[j][i]) {
        M5Cardputer.Display.drawPixel((int32_t)DISPLAY_CENTER(i), (int32_t)j, fb[j * LCD_HEIGHT / DEST_H][i]);
      }
      swap_fb[j][i] = fb[j * LCD_HEIGHT / DEST_H][i];
    } 
  }
}

// Draw a frame to the display without scaling.
// Not normally called. Edit the code to use this function
void draw_frame(uint32_t fb[144][160]) {
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

// Shorten ROM display names if they're too long.
// Memory alloc with C strings is hard so this goes unused for now
//char* clamp_str(char* input) {
//  if(strlen(input) > 10) {
//    char* output = (char*)malloc(sizeof(char)*4+sizeof(char)*strlen(input));
//    for (int i = 0; i < 9; i++) {
//      output[i] = input[i];
//    }
//    sprintf(output, "%s...", input);
//    return output;
//  } else {
//    return input;
//  }
//}

void set_font_size(int size) {
  int textsize = M5Cardputer.Display.height() / size;
  if(textsize == 0) {
    textsize = 1;
  }
  M5Cardputer.Display.setTextSize(textsize);
}

// Opens a ROM file picker menu
// and returns a string containing
// the path of the picked ROM.
char* file_picker() {
  // Open SD card root dir.
  File root_dir = SD.open("/");
  String file_list[MAX_FILES];
  int file_list_size = 0;

  // Look for .gb files in the root dir.
  while(1) {
    File file_entry = root_dir.openNextFile();
    // If we checked all files, stop searching
    if(!file_entry) {
      break;
    }

    // Chec if the entry is a file
    if(!file_entry.isDirectory()) {
      // Get the file extension
      String file_name = file_entry.name();
      String file_extension = file_name.substring(file_name.lastIndexOf(".") + 1);

      // Convert the file extension to lowercase
      //
      // WARNING: major yapping ahead
      //
      // Note: SD cards might have to be formatted as FAT32 
      // to work with this library; if that's the case then
      // doing this doesn't make a difference because FAT32
      // is case insensitive. However, the author of
      // https://github.com/shikarunochi/CardputerSimpleLaucher
      // (which much of the code in this function is edited from)
      // added this line so I guess I can't be too sure.
      // It's not like saving CPU cycles is important here
      // Since this is only called when the menu is first shown so
      //
      // (not like any of this code is particularly efficient)
      //
      // yapping sesh over
      file_extension.toLowerCase();

      if(!file_extension.equals("gb")) {
        continue;
      }

      // Add the ROM's filename to the array
      file_list[file_list_size] = file_name;
      file_list_size++;
    }

    file_entry.close();
  }

  root_dir.close();

  // Boolean to check if a file has been picked.
  // If so we should start the game (ofc lol)  
  bool file_picked = false;
  int select_index = 0;

  M5Cardputer.Display.clearDisplay();

  // This might be kinda stupid but
  // File.name() returns an Arduino-style
  // String object, when Peanut-GB being
  // written in C expects a plain old
  // char array in its `read_rom_to_ram`
  // callback, so we'll need to "convert" these strings
  char* file_list_cstr[MAX_FILES];
  for(int i = 0; i < file_list_size; i++) {
    file_list_cstr[i] = (char*)malloc(sizeof(char)*MAX_FILES);
    file_list[i].toCharArray(file_list_cstr[i], MAX_FILES);
  }

  // Menu loop
  while(!file_picked) {
    // Read Keyboard matrix
    M5Cardputer.update();
    if(M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
      M5Cardputer.Display.clearDisplay();
      for(auto i : status.word) {
        // Controls:
        // Up: e
        // Down: s
        // Play: l
        switch(i) {
          case ';':
            select_index--;
            delay(300);
            break;
          case '.':
            select_index++;
            delay(300);
            break;
          default:
            break;
        }
      }

      if(status.enter) {
        file_picked = true;
      }
    }

    // Loop over list if we went over its bounds
    // e.g. user presses up on the first element
    // or down on the last one
    if(select_index < 0) {
      select_index = file_list_size-1;
    } else if(select_index > file_list_size-1) {
      select_index = 0;
    }

    // Render controls
    // M5Cardputer.Display.drawString("Up/Down: E/S; Select: L", 0, 0);

    //M5Cardputer.Display.setTextDatum(MC_DATUM);

    const int dispW = M5Cardputer.Display.width();
    const int dispH = M5Cardputer.Display.height();

    // Render list
    for(int i = 0; i < file_list_size; i++) {
      // Add an arrow to point to 
      // the currently selected file

      if(select_index == i) {
        set_font_size(64);
        int textW = M5Cardputer.Display.textWidth(file_list_cstr[i]);
        int textH = M5Cardputer.Display.fontHeight();

        M5Cardputer.Display.drawString(" > ", 0, (dispH/2)-(textH/2));
        M5Cardputer.Display.drawString(file_list_cstr[i], (dispW/2)-(textW/2), (dispH/2)-(textH/2));
      } else if(i == select_index-1) {
        set_font_size(128);
        int textW = M5Cardputer.Display.textWidth(file_list_cstr[i]);
        int textH = M5Cardputer.Display.fontHeight();

        M5Cardputer.Display.drawString(file_list_cstr[i], (dispW/2)-(textW/2), (dispH/2)-(textH/2)-textH*2);
      } else if(i == select_index+1) {
        set_font_size(128);
        int textW = M5Cardputer.Display.textWidth(file_list_cstr[i]);
        int textH = M5Cardputer.Display.fontHeight();

        M5Cardputer.Display.drawString(file_list_cstr[i], (dispW/2)-(textW/2), (dispH/2)-(textH/2)+textH*2);
      }
    }
  }

  // Return '/' + selected file path
  char* selected_path = (char*)malloc(sizeof(char)*MAX_FILES+sizeof(char));
  sprintf(selected_path, "/%s", file_list_cstr[select_index]);
  return selected_path;
}

#if ENABLE_SOUND
void audioSetup() {
  // headache. stopped here lol
}
#endif

void setup() {
  // put your setup code here, to run once:

  // Init M5Stack and M5Cardputer libs.
  auto cfg = M5.config();
  // Use keyboard.
  M5Cardputer.begin(cfg, true);

#if ENABLE_SOUND
  M5Cardputer.Speaker.begin();
#endif

  // Set display rotation to horizontal.
  M5Cardputer.Display.setRotation(1);
  set_font_size(64);

  // Initialize SD card.
  // Some of this code is taken from
  // https://github.com/shikarunochi/CardputerSimpleLaucher
  debugPrint("Waiting for SD Card to Init...");
  SPI2.begin(
      M5.getPin(m5::pin_name_t::sd_spi_sclk),
      M5.getPin(m5::pin_name_t::sd_spi_miso),
      M5.getPin(m5::pin_name_t::sd_spi_mosi),
      M5.getPin(m5::pin_name_t::sd_spi_ss));
  while (false == SD.begin(M5.getPin(m5::pin_name_t::sd_spi_ss), SPI2)) {
    delay(1);
  }

  // Initialize GameBoy emulation context.
  static struct gb_s gb;
  static struct priv_t priv;
  enum gb_init_error_e ret;
  debugPrint("postInit");

  
  debugPrint("Before filepick");
  char* selected_file = file_picker();
  debugPrint(selected_file);
  debugPrint("After filepick");

  // Check for errors in reading the ROM file.
  if((priv.rom = read_rom_to_ram(selected_file)) == NULL) {
    // error reporting
    debugPrint("Error at read_rom_to_ram!!");
  }

  ret = gb_init(&gb, &gb_rom_read, &gb_cart_ram_read, &gb_cart_ram_write, &gb_error, &priv);

  if(ret != GB_INIT_NO_ERROR) {
    // error reporting
    debugPrint("GB_INIT error!!!");
  }

  priv.cart_ram = (uint8_t*)malloc(gb_get_save_size(&gb));

#if ENABLE_LCD
  gb_init_lcd(&gb, &lcd_draw_line);
  // Disable interlacing (this is default behaviour but ¯\_(ツ)_/¯)
  gb.direct.interlace = 0;
#endif

  debugPrint("Before loop");

  // Clear the display of any printed text before starting emulation.
  M5Cardputer.Display.clearDisplay();
  
  // Target game speed.
  const double target_speed_us = 1000000.0 / VERTICAL_SYNC;
  while(1) {
    // Variables needed to get steady frametimes.
    int_fast16_t delay;
    unsigned long start, end;
    struct timeval timecheck;

    // Get current timer value
    gettimeofday(&timecheck, NULL);
    start = (long)timecheck.tv_sec * 1000000 +
      (long)timecheck.tv_usec;

    // Reset Joypad
    // This works because button status
    // is stored as a single 8-bit value,
    // with 1 being the non-pressed state.
    // This sets all bits to 1
    gb.direct.joypad = 0xff;

    // Read Keyboard matrix
    M5Cardputer.update();
    if(M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
      // The Cardputer can detect up to 3 key updates at one time
      for(auto i : status.word) {
        // Controls:
        //      e
        //     |=|                     [A]
        // a |=====| d             [B]  l
        //     |=|       //  //     k
        //      s         2  1
        //
        // Might implement a config file to set these.
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

    // Draw the current frame to the screen.
    fit_frame(priv.fb);

    // Get the time after running the CPU and rendering a frame.
    gettimeofday(&timecheck, NULL);
    end = (long)timecheck.tv_sec * 1000000 +
            (long)timecheck.tv_usec;

    // Subtract time taken to render a frame to the target speed.
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

// Unused as I'm using an infinite while-loop
// inside the main function because otherwise
// I'd need to deal with global variables
// which are stupid (doing that gave me an
// ambiguous compiler error so I no no wanna)
void loop() {

}
