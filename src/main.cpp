#include <Arduino.h>
#include "FS.h"
#include "FFat.h"
#include "ESP.h"
#include "vfs_api.h"

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

#if ARDUINO_USB_MODE
#warning This sketch should be used when USB is in OTG mode
void setup() {}
void loop() {}
#else

#include "USB.h"
#include "USBMSC.h"

// #if ARDUINO_USB_CDC_ON_BOOT
// #define HWSerial Serial0
// #define USBSerial Serial
// #else
// #define HWSerial Serial
// USBCDC CDC;
// #endif

USBMSC MSC;

#define USB_MSC_NONE 0
#define USB_MSC_FAT 1
uint8_t usb_msc_mode = USB_MSC_FAT;

wl_handle_t wlh_msc = WL_INVALID_HANDLE;

static esp_err_t msc_strage_init(char *partition_label)
{
    const esp_partition_t *data_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, partition_label);
    if (data_partition == NULL)
    {
        log_e("Failed to find FATFS partition. Check the partition table. %s", partition_label);
        return ESP_ERR_NOT_FOUND;
    }

    return wl_mount(data_partition, &wlh_msc);
}

static esp_err_t msc_storage_read_sector(uint32_t lba,
        uint32_t offset,
        size_t size,
        void *dest)
{
    assert(wlh_msc != WL_INVALID_HANDLE);
    size_t sector_size = wl_sector_size(wlh_msc);

    size_t temp = 0;
    size_t addr = 0; // Address of the data to be read, relative to the beginning of the partition.
    if (!(!__builtin_umul_overflow(lba, sector_size, &temp)))
    {
        log_e("overflow lba %lu sector_size %u", lba, sector_size);
        return ESP_ERR_INVALID_SIZE;
    }
    if (!(!__builtin_uadd_overflow(temp, offset, &addr)))
    {
        log_e("overflow addr %u offset %lu", temp, offset);
        return ESP_ERR_INVALID_SIZE;
    }

    return wl_read(wlh_msc, addr, dest, size);
}

static esp_err_t msc_storage_write_sector(uint32_t lba,
        uint32_t offset,
        size_t size,
        const void *src)
{
    assert(wlh_msc != WL_INVALID_HANDLE);
    size_t sector_size = wl_sector_size(wlh_msc);
    size_t temp = 0;
    size_t addr = 0; // Address of the data to be read, relative to the beginning of the partition.
    if (!(!__builtin_umul_overflow(lba, sector_size, &temp)))
    {
        log_e("overflow lba %lu sector_size %u", lba, sector_size);
        return ESP_ERR_INVALID_SIZE;
    }
    if (!(!__builtin_uadd_overflow(temp, offset, &addr)))
    {
        log_e("overflow addr %u offset %lu", temp, offset);
        return ESP_ERR_INVALID_SIZE;
    }

    if (addr % sector_size != 0 || size % sector_size != 0) {
        log_e("Invalid Argument lba(%lu) offset(%lu) size(%u) sector_size(%u)", lba, offset, size, sector_size);
        return ESP_ERR_INVALID_ARG;
    }

    if (wl_erase_range(wlh_msc, addr, size) != ESP_OK)
    {
        log_e("Failed to erase");
        return ESP_ERR_INVALID_SIZE;
    }

    return wl_write(wlh_msc, addr, src, size);
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    log_v("MSC WRITE: lba: %u, offset: 0x%X, bufsize: 0x%X\n", lba, offset, bufsize);
    esp_err_t err = msc_storage_write_sector(lba, offset, bufsize, buffer);
    if (err != ESP_OK) {
        log_e("msc_storage_write_sector failed: 0x%x", err);
        return 0;
    }
    return bufsize;
}

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    log_v("MSC READ: lba: %u, offset: 0x%X, bufsize: 0x%X\n", lba, offset, bufsize);
    esp_err_t err = msc_storage_read_sector(lba, offset, bufsize, buffer);
    if (err != ESP_OK) {
        log_e("msc_storage_read_sector failed: 0x%x", err);
        return 0;
    }
    return bufsize;
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject)
{
    Serial.printf("MSC START/STOP: power: %u, start: %u, eject: %u\n", power_condition, start, load_eject);
    return true;
}

static void usbEventCallback(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == ARDUINO_USB_EVENTS)
    {
        arduino_usb_event_data_t *data = (arduino_usb_event_data_t *)event_data;
        switch (event_id)
        {
        case ARDUINO_USB_STARTED_EVENT:
            Serial.printf("USB PLUGGED\n");
            break;
        case ARDUINO_USB_STOPPED_EVENT:
            Serial.printf("USB UNPLUGGED\n");
            break;
        case ARDUINO_USB_SUSPEND_EVENT:
            Serial.printf("USB SUSPENDED: remote_wakeup_en: %u\n", data->suspend.remote_wakeup_en);
            break;
        case ARDUINO_USB_RESUME_EVENT:
            Serial.printf("USB RESUMED\n");
            break;

        default:
            break;
        }
    }
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if (!root)
    {
        Serial.printf("- failed to open directory\n");
        return;
    }
    if (!root.isDirectory())
    {
        Serial.printf(" - not a directory\n");
        return;
    }

    File file = root.openNextFile();
    while (file)
    {
        if (file.isDirectory())
        {
            Serial.printf("  DIR : ");
            Serial.printf(file.name());
            if (levels)
            {
                listDir(fs, file.path(), levels - 1);
            }
        }
        else
        {
            Serial.printf("  FILE: ");
            Serial.print(file.name());
            Serial.printf("\tSIZE: ");
            Serial.print(file.size());
            Serial.printf("\n");
        }
        file = root.openNextFile();
    }
}

void readFile(fs::FS &fs, const char *path)
{
    Serial.printf("Reading file: %s\n", path);

    File file = fs.open(path);
    if (!file || file.isDirectory())
    {
        Serial.printf("- failed to open file for reading\n");
        return;
    }

    Serial.printf("- read from file:\n");
    while (file.available())
    {
        Serial.write(file.read());
    }
    file.close();
}

void writeFile(fs::FS &fs, const char *path, const char *message)
{
    Serial.printf("Writing file: %s\n", path);

    File file = fs.open(path, FILE_WRITE);
    if (!file)
    {
        Serial.printf("- failed to open file for writing\n");
        return;
    }
    if (file.print(message))
    {
        Serial.printf("- file written\n");
    }
    else
    {
        Serial.printf("- write failed\n");
    }
    file.close();
}

void appendFile(fs::FS &fs, const char *path, const char *message)
{
    Serial.printf("Appending to file: %s\n", path);

    File file = fs.open(path, FILE_APPEND);
    if (!file)
    {
        Serial.printf("- failed to open file for appending\n");
        return;
    }
    if (file.print(message))
    {
        Serial.printf("- message appended\n");
    }
    else
    {
        Serial.printf("- append failed\n");
    }
    file.close();
}

void renameFile(fs::FS &fs, const char *path1, const char *path2)
{
    Serial.printf("Renaming file %s to %s\n", path1, path2);
    if (fs.rename(path1, path2))
    {
        Serial.printf("- file renamed\n");
    }
    else
    {
        Serial.printf("- rename failed\n");
    }
}

void deleteFile(fs::FS &fs, const char *path)
{
    Serial.printf("Deleting file: %s\n", path);
    if (fs.remove(path))
    {
        Serial.printf("- file deleted\n");
    }
    else
    {
        Serial.printf("- delete failed\n");
    }
}

void testFileIO(fs::FS &fs, const char *path)
{
    Serial.printf("Testing file I/O with %s\n", path);

    static uint8_t buf[512];
    size_t len = 0;
    File file = fs.open(path, FILE_WRITE);
    if (!file)
    {
        Serial.printf("- failed to open file for writing\n");
        return;
    }

    size_t i;
    Serial.printf("- writing\n");
    uint32_t start = millis();
    for (i = 0; i < 2048; i++)
    {
        if ((i & 0x001F) == 0x001F)
        {
            Serial.printf(".");
        }
        file.write(buf, 512);
    }
    Serial.printf("");
    uint32_t end = millis() - start;
    Serial.printf(" - %u bytes written in %u ms\n", 2048 * 512, end);
    file.close();

    file = fs.open(path);
    start = millis();
    end = start;
    i = 0;
    if (file && !file.isDirectory())
    {
        len = file.size();
        size_t flen = len;
        start = millis();
        Serial.printf("- reading\n");
        while (len)
        {
            size_t toRead = len;
            if (toRead > 512)
            {
                toRead = 512;
            }
            file.read(buf, toRead);
            if ((i++ & 0x001F) == 0x001F)
            {
                Serial.printf(".");
            }
            len -= toRead;
        }
        Serial.printf("");
        end = millis() - start;
        Serial.printf("- %u bytes read in %u ms\n", flen, end);
        file.close();
    }
    else
    {
        Serial.printf("- failed to open file for reading\n");
    }
}

void testFat(void)
{
    if (!FFat.begin(false, "/ffat", 10, "ffat"))
    {
        Serial.printf("Mount Failed\n");
    }
    else
    {
        Serial.printf("Total space: %10u\n", FFat.totalBytes());
        Serial.printf("Free space: %10u\n", FFat.freeBytes());
        listDir(FFat, "/", 0);
        writeFile(FFat, "/hello.txt", "Hello ");
        appendFile(FFat, "/hello.txt", "World!\r\n");
        readFile(FFat, "/hello.txt");
        renameFile(FFat, "/hello.txt", "/foo.txt");
        readFile(FFat, "/foo.txt");
        deleteFile(FFat, "/foo.txt");
        readFile(FFat, "/README.txt");
    }
}

void setup()
{
    delay(3000);
    Serial.begin(115200);
    // Serial.setDebugOutput(true);
    log_e("boot");

    if (usb_msc_mode == USB_MSC_NONE)
    {
        testFat();
    }
    else
    {
        if (msc_strage_init("ffat") != ESP_OK)
        {
            Serial.printf("msc_strage_init Failed\n");
        }
        else
        {
            Serial.printf("msc_strage_init OK\n");
            if (wlh_msc == WL_INVALID_HANDLE)
            {
                Serial.printf("wl_handle invalid\n");
            }
            else
            {
            }
        }
    }

    if (usb_msc_mode == USB_MSC_FAT && wlh_msc != WL_INVALID_HANDLE)
    {
        size_t s = wl_size(wlh_msc);
        size_t ss = wl_sector_size(wlh_msc);
        Serial.printf("wl_handle OK size %u sector_size %u\n", s, ss);
        USB.onEvent(usbEventCallback);
        MSC.vendorID("esp32");      // max 8 chars
        MSC.productID("USB_MSC");   // max 16 chars
        MSC.productRevision("1.0"); // max 4 chars
        MSC.onStartStop(onStartStop);
        MSC.onRead(onRead);
        MSC.onWrite(onWrite);
        MSC.mediaPresent(true);
        MSC.begin(s / ss, ss);
        // CDC.begin();
        // USB.begin();
    }


    // initialize LED digital pin as an output.
    // pinMode(LED_BUILTIN, OUTPUT);
}

void loop()
{
    // put your main code here, to run repeatedly:

    // turn the LED on (HIGH is the voltage level)
    // digitalWrite(LED_BUILTIN, HIGH);
    // Serial.printf("high\n");
    // wait for a second
    delay(1000);
    // turn the LED off by making the voltage LOW
    // digitalWrite(LED_BUILTIN, LOW);
    Serial.printf("low\n");
    // wait for a second
    delay(1000);
}

#endif /* ARDUINO_USB_MODE */