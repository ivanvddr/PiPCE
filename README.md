# PiPCE
PiPCE is a PC Engine / TurboGrafx-16 emulator developed for the Raspberry Pi Pico (RP2040) using the Arduino IDE environment.
The project integrates support for the ILI9341 SPI display, HuCard (.pce) image loading from SD card, controls input, and digital I2S audio output.

# 🚀 Features
  -  Core: emulation of the HuC6280 CPU @ 7.16 MHz (with the 1.79 MHz low-speed mode via CSH/CSL), HuC6270 VDC (64 KB VRAM, hardware sprites with 16-per-line limit and overflow flag, raster IRQ, VRAM/SATB DMA) and HuC6260 VCE (9-bit 512-entry color palette).
  -  Display: ILI9341 320×240 SPI support, line-by-line rendering for faithful raster effects, dynamic resolution read from the VDC registers (256 / 352 / 512 pixel modes).
  -  Storage: .pce image loading from MicroSD (with LittleFS fallback), installed into the Pico flash and executed in XIP (zero RAM), with on-demand streaming for oversized cards.
  -  Audio: I2S digital output at 44100 Hz to a MAX98357A DAC/amplifier, 6-channel PSG (32-sample wavetables, DDA mode, LFSR noise, per-channel pan).
  -  Input: 5-way joystick (via GPIO), Fire I / Fire II, SELECT and RUN.
  -  Menu: main menu + game browser with alphabetical list and paging.

# 🛠️ Hardware Requirements
To build the project, you will need the following components:

 - Microcontroller: Raspberry Pi Pico (RP2040). 2 MB of flash are sufficient; with a 4 MB board you can enlarge the resident ROM area (see `configs.h`).
       <br/><!-- Optional: add here a photo of the Pico (see the PiGG README) -->

 - Display: 2.8" ILI9341 SPI (320×240) with integrated SD reader.
       <br/><!-- Optional: add here a photo of the display -->

 - Audio:
    - MAX98357A I2S DAC + 3W class-D amplifier.
          <br/><!-- Optional: add here a photo of the module -->

    - 3W / 4–8Ω speaker (connected in BTL between + and −).
          <br/><!-- Optional: add here a photo of the speaker -->

 - Input:
    - 1× directional board with 5-way switch + 4 buttons (Fire I, Fire II, SELECT, RUN).
          <br/><!-- Optional: add here a photo of the controls -->

# 💾 Card images (.pce)
PC Engine HuCard images are self-booting: **no BIOS / boot ROM is required**. The CPU reset vector is read directly from the card image, and launching a game performs a full reset of CPU/VDC/PSG and re-inserts the card.

1. Place the `.pce` files on the SD card (see the SD Card Structure section).
2. Select the game in the browser: the image is installed into a raw area of the Pico flash and then executed in XIP (execute-in-place), so no RAM is used to hold it.
3. Card images of any size ≥ 8 KB are supported; 512-byte copier headers are detected automatically (by file size) and skipped.

Installation details:<br/>
    -  The first min(size, flash area) bytes are copied into raw flash at `ROM_FLASH_OFFSET`: identical 4 KB sectors are skipped, so relaunching the same game is nearly instant (a progress bar is shown during the copy).
    -  The remainder of larger images stays in the file and is read on demand in 8 KB pages through an 8-page LRU cache in RAM (64 KB, allocated only if needed).

Supported mappers:<br/>
    -  Standard banking: 8 KB banks on the MPR windows; 384 KB cards repeat the 256 KB + 128 KB halves, 512 KB cards repeat the upper half.
    -  Cards ≥ 2 MB (e.g. Street Fighter II'): window mapper with 512 KB windows selected by writes to $1FF0–$1FF3 of bank 0.
    -  Populous: 32 KB of extra SRAM on banks $40–$43, auto-detected from the POPULOUS string at card offset $1F26.

With the default 2 MB flash ("Sketch: 1MB, FS: 1MB" in the Arduino Tools menu) the ROM area is the last 512 KB of the sketch zone (0x080000–0x0FFFFF); the sketch must therefore stay below 512 KB (an explicit error is shown otherwise). With a 4 MB board ("Sketch: 2MB, FS: 2MB") set `ROM_FLASH_OFFSET` to 0x100000 and `ROM_FLASH_SIZE` to 0x100000 in `configs.h`.

# 🔌 Wiring
The Raspberry Pi Pico is the core of the system. Below are the detailed connections, divided by module.

## 1. ILI9341 Display & SD Card

The display and SD card use two separate SPI buses to ensure maximum loading speed and video refresh rate.
The display pins are defined in the TFT_eSPI `User_Setup.h` (the table below shows the same wiring used by the author's PiLynx project); the SD pins are hardcoded in `configs.h`.

| Component | Module Pin | Pico Pin (GPIO) | Note |
|----------|-------------|------------------|------|
| Display  | VCC         | VSYS             | Powered directly from Pico |
|          | GND         | GND              | |
|          | SDO (MISO)  | GP16             | Must match User_Setup.h |
|          | LED         | 3V3              | |
|          | SCK         | GP18             | Must match User_Setup.h |
|          | SDI (MOSI)  | GP19             | Must match User_Setup.h |
|          | DC          | GP21             | Must match User_Setup.h |
|          | RESET       | GP20             | Must match User_Setup.h |
|          | CS          | GP17             | Must match User_Setup.h |
| SD Card  | SD_CS       | GP13             | |
|          | SD_SCK      | GP10             | |
|          | SD_MOSI     | GP11             | |
|          | SD_MISO     | GP12             | |

## 2. Audio (I2S → MAX98357A)

Audio is generated digitally through the I2S peripheral at 44100 Hz: no external low-pass filter is needed, because the MAX98357A integrates the DAC and the class-D amplifier.

| MAX98357A | Pico Pin (GPIO) | Note |
|-----------|-----------------|------|
| VIN       | VSYS (5V)       | Common GND shared with the Pico |
| GND       | GND             | |
| BCLK      | GP26            | |
| LRC       | GP27            | Generated automatically by the library (adjacent GPIO, not separately configurable) |
| DIN       | GP28            | Data to the DAC |
| + / −     | Speaker         | BTL output: connect the speaker ONLY between + and −, never to GND |

- Samples are sent as a true stereo pair (the PSG emulates the per-channel pan); the MAX98357A mixes them down to its mono speaker output.
- The GAIN pin can be left unconnected (default gain).

## 3. Controls (Joystick + Buttons)

All inputs use internal pull-up resistors.
Each button closes the contact to GND (software debounced, 15 ms).

| GPIO | PC Engine Function |
|------|---------------------|
| GP2  | UP |
| GP3  | DOWN |
| GP4  | LEFT |
| GP5  | RIGHT |
| GP22 | CENTER (5th way of the directional switch) — hold ~0.8 s to exit to the menu |
| GP6  | Fire I |
| GP7  | Fire II |
| GP0  | SELECT |
| GP1  | RUN |

# 📝 Technical Notes

- Power:<br/>
    Pico powered via USB.<br/>
    Display VCC from VSYS (5V required for the SD reader), LED backlight from 3V3.<br/>
    MAX98357A powered from 5V with a common GND shared with the Pico.

- Dual-core architecture:<br/>
    CORE0: HuC6280 CPU + VDC/VCE + PSG + input. The VDC is rendered LINE BY LINE (faithful raster effects) in strips of 16 lines; the PSG produces ~2.8 stereo samples per line (44100 Hz / 15734 lines-per-second) into a lock-free SPSC ring buffer.<br/>
    CORE1: strip blit to the TFT via DMA + audio pump from the ring to the I2S library (MAX98357A). The blocking `write16()` acts as the brake (backpressure), so the I2S bit-clock is the timebase of the whole emulation.<br/>
    Handshakes: CORE0 pauses the blit and takes over the display when menu/browser are shown; during flash programming CORE1 stops filling the ring and zero-fills the I2S buffers so the DMA loop replays silence instead of garbage.

- Video:<br/>
    Resolution is derived from the VDC registers at frame start (width up to 512 pixels, active start/height from the VDS/DEW registers; 262/263 lines selected automatically from the VCE clock bit).<br/>
    256-pixel lines are centered on the 320px panel, 352-pixel lines are center-cropped to 320, 512-pixel modes are downscaled 2:1 to 256; the screen is cleared automatically when the size changes.<br/>
    Sprites: 64 in SATB, up to 16 per scanline (overflow flag exposed to the game), 16/32/64 pixel heights, half/two-cell widths, H/V flip, priority bit.<br/>
    The programmable raster IRQ is generated line-accurately (`RASTER_IRQ_OFFSET` / `BYR_WRITE_ADJUST` fine-tuning available in `configs.h`).<br/>
    VCE: 512-entry 9-bit palette converted on the fly to RGB565 (`PCE_COLOR_SWAP` swaps color bytes for alternate wirings).<br/>
    VRAM-to-VRAM DMA with configurable strides/directions; SATB DMA at each VBlank (burst mode or on-demand via CR bit 4).

- CPU & memory map:<br/>
    Full HuC6280 opcode set including the PC Engine extensions: TAM/TMA mapper (8 MPR windows), block transfers TII/TDD/TIN/TIA/TAI, CSL/CSH clock switch, T flag (SET) for zero-page arithmetic, TSB/TRB/TST, BBR/BBS/RMB/SMB, ST0/ST1/ST2 direct VDC access, CLA/CLX/CLY, SAX/SAY/SXY and decimal mode.<br/>
    8 KB work RAM on banks $F8–$FB, 8 KB I/O window on MPR $FF (VDC, VCE, PSG, timer, joypad with select/clear strobing).<br/>
    For speed, the MPR-based bus resolves to direct pointers (fast path) with a slow path only for non-resident ROM pages and I/O; IRQ2/IRQ1/TIMER vectors at $FFF6/$FFF8/$FFFA.<br/>
    8-bit interval timer with IRQ (1024-cycle tick).

- Card storage & flash install:<br/>
    SD reads run on SPI1 at 10 MHz (`SD_SPI_HZ`, raisable to 25 MHz if the card is fast enough: fewer stalls on streamed ROMs).<br/>
    Flash programming is sector-based (4 KB): identical sectors are skipped; CORE1 is idled and interrupts disabled only for the duration of each erase/program cycle.<br/>
    Non-resident ROM pages are fetched from the file in 8 KB blocks; the last partial page is padded with 0xFF.

- Audio:<br/>
    6 channels, 32-sample 5-bit wavetables, DDA mode, LFSR noise on channels 5–6, 32 logarithmic volume levels and 16-step pan per channel; the PSG LFO is not emulated.<br/>
    The 44100 Hz output acts as the system timebase, keeping emulation speed and audio permanently in sync.<br/>
    A guardrail on the ring level decides whether the video frame can be drawn (see Performance).

# 📂 Software Setup

The project is developed for Arduino IDE using the arduino-pico core (Earle Philhower RP2040 core).

## Board Settings

In the Arduino Tools menu select the flash size consistently with `configs.h`:<br/>
    -  Default configuration (Pico 2MB): Flash Size = "2MB (Sketch: 1MB, FS: 1MB)".<br/>
    -  Pico 4MB: Flash Size = "2MB (Sketch: 2MB, FS: 2MB)" and change `ROM_FLASH_OFFSET`/`ROM_FLASH_SIZE` to 0x100000/0x100000 in `configs.h`.

## Required Libraries

Make sure you have installed:<br/>
    - TFT_eSPI (configured for ILI9341 on Pico)<br/>
    - SD, LittleFS and I2S (bundled with the arduino-pico core)

## TFT_eSPI Configuration:<br/>
    Replace User_Setup.h with:<br/>
        - Driver: ILI9341<br/>
        - SPI Frequency: 80MHz<br/>
        - DMA: Enabled

If you notice graphical glitches, reduce SPI frequency to 40–50MHz.

## Useful `configs.h` Tunables

    - PICO_CLOCK_KHZ: system clock (default 300000, see Performance).
    - AUDIO_SAMPLE_RATE: 44100 Hz by default; 32000 / 22050 to relieve CORE0.
    - SD_SPI_HZ: SD clock speed.
    - PSG_VOLUME: global PSG gain.
    - PCE_COLOR_SWAP: swap color bytes if colors look wrong.
    - RASTER_IRQ_OFFSET / BYR_WRITE_ADJUST: fine tuning for raster-sensitive titles.

## SD Card Structure

The emulator looks for files inside the /PiPCE directory.
Organize your SD card like this:<br/>

        PiPCE/
        └─ games/
             ├─ game1.pce
             └─ game2.pce

Up to 200 titles are listed in the browser; keep file names within 31 characters (PCE_NAME_MAX = 32 including the string terminator). The .pce extension match is case-insensitive.

## Using LittleFS Instead of a Physical SD Card

Instead of using a physical SD card, you can store the entire PiPCE/games folder directly inside the Pico's onboard flash memory.<br/>
To do this, use the LittleFS Upload Tool for Arduino IDE, which uploads the contents of your local /data folder into the Pico's flash filesystem (create a `data/PiPCE/games` folder next to the sketch and put your .pce files inside).<br/>
Official documentation:<br/>
Arduino-Pico LittleFS guide:<br/>
https://arduino-pico.readthedocs.io/en/latest/filesystems.html<br/>
LittleFS Upload Tool (arduino-littlefs-upload):<br/>
https://github.com/earlephilhower/arduino-littlefs-upload

# 🎮 Usage

1. **Startup**: On boot, the main menu appears with the PiPCE title screen.<br/>
2. **Navigation**: Use UP/DOWN to switch between the entries and Fire I to select:<br/>
    **GAMES BROWSER**: Opens the file browser to load a .pce from SD/LittleFS<br/>
    **RESUME GAME**: Restores the suspended game exactly as it was (greyed out until the first game is launched)<br/>
3. **Game Browser**: Titles are listed alphabetically, 10 per page; UP/DOWN moves by one entry, LEFT/RIGHT by 10; the header shows the filesystem in use (SD/LittleFS) and the position counter. Press Fire I to boot the selected .pce, Fire II to go back to the menu.<br/>
4. **Loading**: The selected image is installed into the flash area with a progress bar ("LOADING ROM"); errors (file too small, sketch too big for the ROM area, read/verify failures) are shown in red.<br/>
5. **In game**: SELECT and RUN work as the console pad keys. Hold CENTER for ~0.8 s to exit to the menu without touching the emulated state: "RESUME GAME" will restart from that exact point.<br/>

# ⚡ Performance & Overclock

To achieve acceptable emulation speed, the Raspberry Pi Pico is pushed beyond its factory specifications.<br/>
In configs.h:

        #define PICO_CLOCK_KHZ 300000 // Overclock to 300MHz

The core voltage is raised to 1.30V (VREG_VOLTAGE_1_30) and the requested frequency is verified at runtime against the real system clock (with fallback to 250 MHz). The peripheral clock is driven from the system clock, so the TFT SPI bus can run up to clk_sys/2.<br/>
This frequency allows:<br/>
    - Real-time execution of the 7.16 MHz HuC6280 core with VDC/VCE and PSG
    - 44100 Hz I2S output used as the emulation timebase
    - SPI DMA strip blit with audio pumping interleaved during the transfers

Timing strategy (real timing > framerate):<br/>
    - The CPU runs on a per-scanline tick budget (455 ticks @ 7.16 MHz) with a carry, so emulation is paced on emulated time.<br/>
    - `pceRunFrame()` blocks when the audio ring is full (= real time has elapsed) and the video frame is not drawn when the audio ring drops below 96 samples (~2.2 ms), for at most 3 consecutive frames; emulation always continues.<br/>

You may still notice slowdowns in some games, graphical glitches, or audio ticks due to timing desynchronization.

# ⚠️ Disclaimer

This project is provided "as is", for hobby and experimental use.<br/>
Overclock: 300 MHz with 1.30V core voltage may reduce microcontroller lifespan or cause thermal instability.<br/>
Hardware: The author is not responsible for damage to components (Pico, display, amplifier, speaker), overheating, or malfunctions caused by incorrect wiring or use of the provided software.<br/>
Risks: Any hardware or software modification is performed at the user's own risk.<br/>
Software: The game ROMs are copyrighted works — dump them only from hardware/media you legally own. This repository does not contain and does not distribute any.

# 📄 License & Credits

Released under the GNU General Public License v3.0 (see LICENSE).<br/>
Menu, game browser, storage layer and audio pipeline derive from the author's PiLynx / Pi64 projects.
**Author**: Ivan Vettori *ivanvddr*
**License**: GNU GPL v3.0

