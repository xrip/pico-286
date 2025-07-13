# Port Documentation

This document provides a brief description of the I/O ports used in this emulator.

## `src/emulator/ports.c`

### `portout`

#### Ports `0x00` to `0x0F`
* **i8237 DMA Controller**: These ports are used to control the 8237 Direct Memory Access (DMA) controller. This is used for transferring data between memory and peripherals without involving the CPU.

#### Ports `0x20` and `0x21`
* **i8259 PIC**: These ports are used to control the 8259 Programmable Interrupt Controller (PIC). This is used to manage hardware interrupts.

#### Ports `0x40` to `0x43`
* **i8253 PIT**: These ports are used to control the 8253 Programmable Interval Timer (PIT). This is used for generating timing signals.

#### Port `0x61`
* **PC Speaker**: This port is used to control the PC speaker. This is used for generating simple sounds.

#### Port `0x64`
* **Keyboard Controller**: This port is used to send commands to the keyboard controller.

#### Ports `0x81`, `0x82`, `0x83`, `0x87`
* **i8237 DMA Controller Page Registers**: These ports are used to set the page registers for the 8237 DMA controller. This is used for 20-bit addressing.

#### Port `0x92`
* **A20 Gate**: This port is used to control the A20 line. This is used to access memory above 1MB.

#### Ports `0xC0` to `0xC7`, `0x1E0`, `0x2C0`
* **Tandy 3-Voice Sound**: These ports are used to control the Tandy 3-voice sound chip. This is a sound chip that can generate three square waves and one noise channel.

#### Port `0x201`
* **Joystick**: This port is used to read the state of the joystick.

#### Ports `0x220` to `0x223`
* **Creative Music System / GameBlaster**: These ports are used to control the Creative Music System (CMS) or GameBlaster sound card. This is a sound card that uses two SAA1099 chips to generate sound.

#### Ports `0x224` to `0x22F`
* **Sound Blaster**: These ports are used to control the Sound Blaster sound card. This is a popular sound card that includes a DAC, an FM synthesizer, and a joystick port.

#### Ports `0x260` to `0x263`
* **EMS**: These ports are used to control Expanded Memory Specification (EMS). This is a method of accessing memory above 1MB on 8088/8086 processors.

#### Port `0x278`
* **Covox Speech Thing**: This port is used to send data to the Covox Speech Thing. This is a simple DAC that connects to the parallel port.

#### Ports `0x330` and `0x331`
* **MPU-401**: These ports are used to control the MPU-401 MIDI interface. This is used to connect to external MIDI devices.

#### Ports `0x378` and `0x37A`
* **Disney Sound Source**: These ports are used to control the Disney Sound Source. This is a simple DAC that connects to the parallel port.

#### Ports `0x388` and `0x389`
* **AdLib / OPL2**: These ports are used to control the AdLib sound card, which uses the Yamaha YM3812 (OPL2) chip. This is an FM synthesizer chip that can generate 9 channels of sound.

#### Ports `0x3B0` to `0x3B7` and `0x3D0` to `0x3D7`
* **MC6845 CRTC**: These ports are used to control the Motorola 6845 Cathode Ray Tube Controller (CRTC). This is used to generate the video signals for the monitor.

#### Ports `0x3B8`, `0x3BF`, `0x3D8`, `0x3D9`
* **CGA**: These ports are used to control the Color Graphics Adapter (CGA). This is a video card that can display 4 colors at a resolution of 320x200, or 2 colors at a resolution of 640x200.

#### Ports `0x3DA`, `0x3DE`, `0x3DF`
* **TGA**: These ports are used to control the Tandy Graphics Adapter (TGA). This is a video card that is similar to CGA, but with an extended palette of 16 colors.

#### Ports `0x3F8` to `0x3FF`
* **Serial Port (Mouse)**: These ports are used to control the serial port, which is used for the mouse.

### `portin`

#### Ports `0x240` to `0x257`
* **RTC**: These ports are used to read the Real-Time Clock (RTC). This is used to keep track of the date and time.

#### Port `0x27A`
* **LPT2 Status**: This port is used to read the status of the second parallel port. This is used for the Covox Speech Thing.

#### Port `0x379`
* **Disney Sound Source Status**: This port is used to read the status of the Disney Sound Source.

## `src/emulator/video/cga.c`

### `cga_portout`

#### Port `0x3B8`
* **Hercules Mode Control**: This port is used to control Hercules graphics mode. This is used for monochrome graphics on a TTL monitor.

#### Port `0x3BF`
* **Hercules Enable**: This port is used to enable or disable Hercules graphics mode.

#### Port `0x3D8`
* **CGA Mode Control Register**: This port is used to control the mode of the CGA. This includes setting the video mode, enabling/disabling blinking, and setting the color burst.

#### Port `0x3D9`
* **CGA Color Control Register**: This port is used to control the colors of the CGA. This includes setting the foreground color, the color set, and the intensity.

## `src/emulator/video/tga.c`

### `tga_portout`

#### Port `0x3DA`
* **TGA Address Register**: This port is used to select a TGA register to read from or write to.

#### Port `0x3DE`
* **TGA Data Register**: This port is used to read from or write to the selected TGA register. This is used to control the TGA's palette, border color, and mode.

#### Port `0x3DF`
* **TGA CRT/Processor Page Register**: This port is used to control the memory mapping of the TGA. This is used to select the video memory page that is displayed on the screen, and the page that is accessible to the CPU.

## `src/emulator/video/vga.c`

### `vga_portout`

#### Port `0x3C0`
* **VGA Attribute Address/Data Register**: This port is used to write to the VGA's attribute registers. This is used to control the palette and other attributes of the display.

#### Port `0x3C4`
* **VGA Sequencer Address Register**: This port is used to select a VGA sequencer register to write to.

#### Port `0x3C5`
* **VGA Sequencer Data Register**: This port is used to write to the selected VGA sequencer register. This is used to control the clocking and memory mapping of the VGA.

#### Port `0x3C7`
* **VGA DAC Read Index Register**: This port is used to select a palette register to read from.

#### Port `0x3C8`
* **VGA DAC Write Index Register**: This port is used to select a palette register to write to.

#### Port `0x3C9`
* **VGA DAC Data Register**: This port is used to read from or write to the selected palette register.

#### Port `0x3CE`
* **VGA Graphics Controller Address Register**: This port is used to select a VGA graphics controller register to write to.

#### Port `0x3CF`
* **VGA Graphics Controller Data Register**: This port is used to write to the selected VGA graphics controller register. This is used to control the logical operations between the CPU and the video memory.

### `vga_portin`

#### Port `0x3C8`
* **VGA DAC Read Index Register**: This port is used to read the current DAC read index.

#### Port `0x3C9`
* **VGA DAC Data Register**: This port is used to read the data from the selected palette register.
