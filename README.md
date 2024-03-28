Arduino esp32 is based on esp-idf 4.4. And although the partition created with mkfatfs in Arduino starts at position offset 0, the esp_vfs_fat_spiflash_mount function used by FFat's begin normally recognizes images starting at offset 0x1000. This can be confirmed by looking at the log downloading the fatfs image from Arduino IDE.
Therefore, if the fat partition starts at 0x110000 and has a size of 0x10000, the fat image applied to it must start at 0x111000 and be created with a size of 0xFF000 to be used normally.

The msc_xxx series functions in the source were imported from tusb_msc_storage.c in esp-idf 5.1. esp-idf has increased USB support examples since 5.1.


How to build PlatformIO based project
=====================================

1. [Install PlatformIO Core](https://docs.platformio.org/page/core.html)
2. Download [development platform with examples](https://github.com/platformio/platform-espressif32/archive/develop.zip)
3. Extract ZIP archive
4. Run these commands:

```shell
# Change directory to example
$ cd platform-espressif32/examples/arduino-blink

# Build project
$ pio run

# Upload firmware
$ pio run --target upload

# Build specific environment
$ pio run -e esp32dev

# Upload firmware for the specific environment
$ pio run -e esp32dev --target upload

# Clean build files
$ pio run --target clean
```



# Commands to use in this project
1. Make the data folder into a data.bin file in PlatformIO Core CLI. The size is 0xFF000 for 0x111000. 

```
c:\Users\(name)\.platformio\packages\tool-mkfatfs\mkfatfs.exe -c data -t fatfs -s 1044480 .pio\build\(board)\data.bin  
```

2. Upload data.bin to ringo partition (must write to 0x111000) in PlatformIO Core CLI
```
python "c:\Users\(name)\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32s3 --port (COMPORT) --baud 460800 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_size 8MB 0x111000 .pio\build\(board)\data.bin
```

# Set usb_msc_mode variable to USB_MSC_NONE.
See the log.
```
[    99][D][esp32-hal-tinyusb.c:680] tinyusb_enable_interface(): Interface CDC enabled
[    99][D][esp32-hal-tinyusb.c:680] tiTotal space:    1019904
Free space:    1015808
Listing directory: /
  FILE: README.txt      SIZE: 12
Writing file: /hello.txt
- file written
Appending to file: /hello.txt
- message appended
Reading file: /hello.txt
- read from file:
Hello World!
Renaming file /hello.txt to /foo.txt
- file renamed
Reading file: /foo.txt
- read from file:
Hello World!
Deleting file: /foo.txt
- file deleted
Reading file: /README.txt
- read from file:
hello, world[   878][D][esp32-hal-rmt.c:615] rmtInit():  -- TX RMT - CH 0 - 1 RAM Blocks - pin 48
high
low
high
low
high
low
high
low
```

# Set usb_msc_mode variable to USB_MSC_FAT.
See the log.
```
[    99][D][esp32-hal-tinyusb.c:680] tinyusb_enable_interface(): Interface CDC enabled
[    99][D][esp32-hal-tinyusb.c:680] timsc_strage_init OK
wl_handle OK size 1024000 sector_size 4096
[   111][D][esp32-hal-tinyusb.c:569] tinyusb_load_enabled_interfaces(): Load Done: if_num: 3, descr_len: 98, if_mask: 0x11
[   116][D][esp32-hal-rmt.c:615] rmtInit():  -- TX RMT - CH 0 - 1 RAM Blocks - pin 48
USB UNPLUGGED
high
USB PLUGGED
low
high
low
high
low
```
