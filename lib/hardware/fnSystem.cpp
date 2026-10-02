
#include "fnSystem.h"

#ifdef ESP_PLATFORM

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <driver/gpio.h>
#if CONFIG_IDF_TARGET_ESP32S3
# include <hal/gpio_ll.h>
#else
//# include <driver/dac.h>
#endif
#include <esp_idf_version.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include <esp_chip_info.h>
#include <esp_random.h>
#include <hal/gpio_ll.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#define ADC_WIDTH_12Bit ADC_BITWIDTH_12
#define ADC_ATTEN_11db ADC_ATTEN_DB_12
#else
#include <driver/adc.h>
#include <esp_adc_cal.h>
#define ADC_WIDTH_12Bit ADC_WIDTH_BIT_12
#define ADC_ATTEN_11db ADC_ATTEN_DB_11
#endif
#include <soc/rtc.h>

#include <mlff.h>

// ESP_PLATFORM
#else
// !ESP_PLATFORM

#include <sys/time.h>
#include <unistd.h>
#include <sched.h>
#include "compat_uname.h"
#include "compat_gettimeofday.h"
#include "compat_esp.h" // empty IRAM_ATTR macro for FujiNet-PC
#include "build_version.h"

// !ESP_PLATFORM
#endif

#include <time.h>
#include <cstring>

#include "../../include/debug.h"
#include "../../include/version.h"
#include "../../include/pinmap.h"

#include "bus.h"

#include "fsFlash.h"
#include "fnFsSD.h"
#include "fnWiFi.h"
#include "fnLedStrip.h"

    #define TARGET_PLATFORM_NAME "RS232"

#ifdef ESP_PLATFORM
static QueueHandle_t card_detect_evt_queue = NULL;

static void IRAM_ATTR card_detect_isr_handler(void *arg)
{
    // Generic default interrupt handler
    gpio_num_t gpio_num = (gpio_num_t)(int)arg;
    xQueueSendFromISR(card_detect_evt_queue, &gpio_num, NULL);
    //Debug_printf("INTERRUPT ON GPIO: %d", gpio_num);
}

static void card_detect_intr_task(void *arg)
{
    // Assert valid initial card status
    vTaskDelay(1);
    // Set card status before we enter the infinite loop
#ifdef CARD_DETECT_HIGH
    int card_detect_status = !gpio_get_level((gpio_num_t)(int)arg);
#else
    int card_detect_status = gpio_get_level((gpio_num_t)(int)arg);
#endif

    for (;;) {
        gpio_num_t gpio_num;
        if(xQueueReceive(card_detect_evt_queue, &gpio_num, portMAX_DELAY)) {
#ifdef CARD_DETECT_HIGH
            int level = !gpio_get_level(gpio_num);
#else
            int level = gpio_get_level(gpio_num);
#endif
            if (card_detect_status == level) {
                printf("SD Card detect ignored (debounce)\r\n");
            }
            else if (level == 1) {
                printf("SD Card Ejected, REBOOT!\r\n");
                fnSystem.reboot();
            }
            else {
                printf("SD Card Inserted\r\n");
                fnSDFAT.start();
            }
            card_detect_status = level;
        }
    }
}

static void setup_card_detect(gpio_num_t pin)
{
    // Create a queue to handle card detect event from ISR
    card_detect_evt_queue = xQueueCreate(10, sizeof(gpio_num_t));
    if (card_detect_evt_queue == nullptr)
    {
        // The ISR would crash sending to a null queue - skip the whole feature.
        Debug_printv("could not create card detect queue, SD hot-swap disabled");
        return;
    }
    // Start card detect task
    if (xTaskCreate(card_detect_intr_task, "card_detect_intr_task", 6144, (void *)pin, 10, nullptr) != pdPASS)
    {
        Debug_printv("could not create card detect task, SD hot-swap disabled");
        return;
    }
    // Enable interrupt for card detection
    fnSystem.set_pin_mode(pin, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_NONE, GPIO_INTR_ANYEDGE);
    // Add the card detect handler
    gpio_isr_handler_add(pin, card_detect_isr_handler, (void *)pin);
}
// ESP_PLATFORM
#else
// !ESP_PLATFORM
// keep reference timestamp
uint64_t _get_start_millis()
{
    struct timeval tv;
    compat_gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec*1000ULL+tv.tv_usec/1000ULL);
}
uint64_t _start_millis = _get_start_millis();
uint64_t _start_micros = _start_millis*1000;
// !ESP_PLATFORM
#endif

// Global object to manage System
SystemManager fnSystem;

SystemManager::SystemManager()
{
    memset(_uptime_string,0,sizeof(_uptime_string));
    memset(_currenttime_string,0,sizeof(_currenttime_string));
#ifndef ESP_PLATFORM
    memset(_uname_string, 0, sizeof(_uname_string));
#else
    ledstrip_found = fnLedStrip.present();
#endif
    _hardware_version=0;
}

// Returns current CPU frequency in MHz
uint32_t SystemManager::get_cpu_frequency()
{
#ifdef ESP_PLATFORM
    rtc_cpu_freq_config_t cfg;
    rtc_clk_cpu_freq_get_config(&cfg);
    return cfg.freq_mhz;
#else
    return 1;
#endif
}

#ifdef ESP_PLATFORM
// Set pin mode
void SystemManager::set_pin_mode(uint8_t pin, gpio_mode_t mode, pull_updown_t pull_mode, gpio_int_type_t intr_type)
{
    gpio_config_t io_conf;

    // Set interrupt (disabled unless specified)
    io_conf.intr_type = intr_type;

    // set mode
    io_conf.mode = mode;

    // set pull-up pull-down modes
    if (pull_mode == PULL_BOTH || pull_mode == PULL_UP)
        io_conf.pull_up_en = gpio_pullup_t::GPIO_PULLUP_ENABLE;
    else
        io_conf.pull_up_en = gpio_pullup_t::GPIO_PULLUP_DISABLE;

    if (pull_mode == PULL_BOTH || pull_mode == PULL_DOWN)
        io_conf.pull_down_en = gpio_pulldown_t::GPIO_PULLDOWN_ENABLE;
    else
        io_conf.pull_down_en = gpio_pulldown_t::GPIO_PULLDOWN_DISABLE;

    // bit mask of the pin to set
    io_conf.pin_bit_mask = 1ULL << pin;

    // configure GPIO with the given settings
    gpio_config(&io_conf);
}
#endif

// from esp32-hal-misc.
// Set DIGI_LOW or DIGI_HIGH
void IRAM_ATTR SystemManager::digital_write(uint8_t pin, uint8_t val)
{
#ifdef ESP_PLATFORM
    gpio_set_level((gpio_num_t)pin, val ? 1 : 0);
#else
    Debug_println("SystemManager::digital_write() not implemented");
#endif
}

// from esp32-hal-misc.
// Returns DIGI_LOW or DIGI_HIGH
int IRAM_ATTR SystemManager::digital_read(uint8_t pin)
{
#ifdef ESP_PLATFORM
    return gpio_get_level((gpio_num_t)pin);
#else
    Debug_println("SystemManager::digital_read() not implemented");
    return 0;
#endif
}

#ifdef ESP_PLATFORM
uint32_t SystemManager::random()
{
    return esp_random();
}

// from esp32-hal-misc.c
unsigned long IRAM_ATTR SystemManager::micros()
{
    return (unsigned long)(esp_timer_get_time());
}

// from esp32-hal-misc.c
unsigned long IRAM_ATTR SystemManager::millis()
{
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

// from esp32-hal-misc.
void SystemManager::delay(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}
#else
uint32_t SystemManager::random()
{
    /* rand() repeats the same sequence from process start unless seeded */
    static bool seeded = false;

    if (!seeded)
    {
        srand((unsigned)time(NULL));
        seeded = true;
    }

    return (uint32_t)rand();
}

uint64_t SystemManager::micros()
{
    struct timeval tv;
    compat_gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec*1000000ULL+tv.tv_usec) - _start_micros;
}

uint64_t SystemManager::millis()
{
    struct timeval tv;
    compat_gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec*1000UL+tv.tv_usec/1000UL) - _start_millis;
}

void SystemManager::delay(uint32_t ms)
{
    usleep(ms*1000);
}
#endif

#ifdef ESP_PLATFORM
// from esp32-hal-misc.
void IRAM_ATTR SystemManager::delay_microseconds(uint32_t us)
{
    uint32_t start = (uint32_t)esp_timer_get_time();

    if (us)
    {
        uint32_t end = start + us;

        // Handle overflow first
        if (start > end)
        {
            while ((uint32_t)esp_timer_get_time() > end)
                NOP();
        }
        while ((uint32_t)esp_timer_get_time() < end)
            NOP();
    }
}
#endif // ESP_PLATFORM

#if defined(__linux__) || defined(__APPLE__)
void SystemManager::delay_microseconds(uint32_t us)
{
    usleep(us);
}
#endif

#if defined(_WIN32)
void SystemManager::delay_microseconds(uint32_t us)
{
    // a)
    // HANDLE timer;
    // LARGE_INTEGER ft;

    // ft.QuadPart = -(10*us); // Convert to 100 nanosecond interval, negative value indicates relative time

    // timer = CreateWaitableTimer(NULL, TRUE, NULL);
    // SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
    // WaitForSingleObject(timer, INFINITE);

    // CloseHandle(timer);

    // b)
    // usleep(us);

    // c)
    // std::this_thread::sleep_for(std::chrono::microseconds(us));

    // d) combination of Sleep and busy-looping
    if (us > 1000000)
    {
        // At least one second. Millisecond resolution is sufficient.
        Sleep(us / 1000);
    }
    else
    {
        // Use Sleep for the largest part, and busy-loop for the rest
        static double frequency;
        if (frequency == 0)
        {
            LARGE_INTEGER freq;
            if (!QueryPerformanceFrequency (&freq))
            {
                Debug_println("QueryPerformanceFrequency failed");
                // Cannot use QueryPerformanceCounter.
                Sleep (us / 1000);
                return;
            }
            frequency = (double) freq.QuadPart / 1000000000.0;
        }
        long long expected_counter_difference = 1000 * us * frequency;
        int sleep_part = (int) us / 1000 - 10;
        LARGE_INTEGER before;
        QueryPerformanceCounter (&before);
        long long expected_counter = before.QuadPart + expected_counter_difference;
        if (sleep_part > 0)
            Sleep(sleep_part);
        for (;;)
        {
            LARGE_INTEGER after;
            QueryPerformanceCounter (&after);
            if (after.QuadPart >= expected_counter)
                break;
        }
    }
}
#endif // _WIN32

#ifdef ESP_PLATFORM
// from esp32-hal-misc.
void SystemManager::yield()
{
    vPortYield();
}
#else
void SystemManager::yield()
{
    sched_yield();
}
#endif

#ifdef ESP_PLATFORM
// TODO: Close open files first
void SystemManager::reboot()
{
    SYSTEM_BUS.shutdown();
    fnWiFi.stop();
    esp_restart();
}
#else
void SystemManager::reboot(uint32_t delay_ms, bool reboot)
{
    if (delay_ms == 0)
    {
        // do cleanup and exit
        Debug_println("SystemManager::reboot - exiting ...");
        // FN will be restarted if ended with EXIT_AND_RESTART (75)
        exit(_reboot_code);
    }
    else
    {
        // deferred reboot
        // called from httpService, some time is needed to finish http request prior exiting
        _reboot_at = millis() + delay_ms;
        _reboot_code = reboot ? EXIT_AND_RESTART : 0;
        Debug_printf("SystemManager::reboot - exit(%d) in %d ms\n", _reboot_code, delay_ms);
    }
}

bool SystemManager::check_deferred_reboot()
{
    return _reboot_at && millis() >= _reboot_at;
}

int SystemManager::request_for_shutdown()
{
    _shutdown_requests = _shutdown_requests + 1;
    return _shutdown_requests;
}
int SystemManager::check_for_shutdown()
{
    return _shutdown_requests;
}
#endif

/* Size of available heap. Size of largest contiguous block may be smaller.
*/
uint32_t SystemManager::get_free_heap_size()
{
#ifdef ESP_PLATFORM
    return esp_get_free_heap_size();
#else
    return 0;
#endif
}

/* Microseconds since system boot-up
*/
#ifdef ESP_PLATFORM
int64_t SystemManager::get_uptime()
{
    return esp_timer_get_time();
}
#else
uint64_t SystemManager::get_uptime()
{
    return micros();
}
#endif

void SystemManager::update_timezone(const char *timezone)
{
#ifdef ESP_PLATFORM
    if (timezone != nullptr && timezone[0] != '\0')
        setenv("TZ", timezone, 1);

    tzset();
#else
    Debug_printf("SystemManager::update_timezone(%s) - not implemented\r\n", timezone);
#endif
}

void SystemManager::update_hostname(const char *hostname)
{
    if (hostname != nullptr && hostname[0] != '\0')
    {
        Debug_printf("SystemManager::update_hostname(%s)\r\n", hostname);
        fnWiFi.set_hostname(hostname);
    }
}

void SystemManager::update_firmware()
{
#ifdef ESP_PLATFORM
    Debug_printf("Stopping flash filesystem...\r\n");
    fsFlash.stop();

    Debug_println("Flash bin files from '/sd/.bin/'");
    mlff_update(PIN_SD_HOST_CS, PIN_SD_HOST_MISO, PIN_SD_HOST_MOSI, PIN_SD_HOST_SCK);

    Debug_println("Reboot to run update app and flash 'main.*.bin'...");
    reboot();
#endif
}

const char *SystemManager::get_current_time_str()
{
    time_t tt = time(nullptr);
    struct tm *tinfo = localtime(&tt);

#if !defined(_WIN32) || defined(_UCRT)
    // compatibility notice:
    // this works on Windows only if linked using newer UCRT lib (Universal C runtime, Windows 10+)
    strftime(_currenttime_string, sizeof(_currenttime_string), "%a %b %e, %H:%M:%S %Y %z", tinfo);
#else
    // this should work on Windows if linked using old MSVCRT lib
    // (%#d instead of %e, no timezone)
    strftime(_currenttime_string, sizeof(_currenttime_string), "%a %b %#d, %H:%M:%S %Y", tinfo);
#endif
    return _currenttime_string;
}

const char *SystemManager::get_uptime_str()
{
#ifdef ESP_PLATFORM
    int64_t ms = esp_timer_get_time();
#else
    int64_t ms = (int64_t)micros();
#endif

    long ml = ms / 1000;
    long s = ml / 1000;
    int m = s / 60;
    int h = m / 60;

    if (h > 0)
        snprintf(_uptime_string, sizeof(_uptime_string), "%02d:%02d:%02ld.%03ld", h, m % 60, s % 60, ml % 1000);
    else
        snprintf(_uptime_string, sizeof(_uptime_string), "%02d:%02ld.%03ld", m, s % 60, ml % 1000);

    return _uptime_string;
}

#ifndef ESP_PLATFORM
const char *SystemManager::get_uname()
{
    struct utsname uts;

    if (uname(&uts) == -1)
        return "Unknown";

#if defined(_WIN32)
    if (snprintf(_uname_string, sizeof(_uname_string), "%s %s.%s %s", uts.sysname, uts.version, uts.release, uts.machine) >= sizeof(_uname_string))
#else
    if (snprintf(_uname_string, sizeof(_uname_string), "%s %s %s", uts.sysname, uts.release, uts.machine) >= sizeof(_uname_string))
#endif
    {
        strcpy(_uname_string+sizeof(_uname_string)-4, "...");
    }
    return _uname_string;
}
#endif // !ESP_PLATFORM

const char *SystemManager::get_sdk_version()
{
#ifdef ESP_PLATFORM
    return esp_get_idf_version();
#else
    return "NOSDK";
#endif
}

const char *SystemManager::get_target_platform_str()
{
    return TARGET_PLATFORM_NAME;
}

const char *SystemManager::get_fujinet_version(bool shortVersionOnly)
{
#ifdef ESP_PLATFORM
    if (shortVersionOnly)
        return FN_VERSION_FULL;
    else
        return FN_VERSION_FULL " " FN_VERSION_DATE " (" TARGET_PLATFORM_NAME ")";
#else
    if (shortVersionOnly)
        return FN_VERSION_FULL_GIT;
    else
        return FN_VERSION_FULL_GIT " " FN_BUILD_GIT_DATE " (" TARGET_PLATFORM_NAME ")";
#endif
}

int SystemManager::get_cpu_rev()
{
#ifdef ESP_PLATFORM
    esp_chip_info_t chipinfo;
    esp_chip_info(&chipinfo);
    return chipinfo.revision;
#else
    return 0;
#endif
}

SystemManager::chipmodels SystemManager::get_cpu_model()
{
#ifdef ESP_PLATFORM
    esp_chip_info_t chipinfo;
    esp_chip_info(&chipinfo);

    switch (chipinfo.model)
    {
    case esp_chip_model_t::CHIP_ESP32:
        return chipmodels::CHIP_ESP32;
        break;
    default:
        return chipmodels::CHIP_UNKNOWN;
        break;
    }
#else
    return chipmodels::CHIP_UNKNOWN;
#endif
}

int SystemManager::get_sio_voltage()
{
#ifdef ESP_PLATFORM

#endif      // ESP_PLATFORM

    return 0;
}

/*
 Create temporary file using provided FileSystem.
 Filename will be 8 characters long. If provided, generated filename will be placed in result_filename
 File opened in "wb+" mode.
*/
FILE *SystemManager::make_tempfile(FileSystem *fs, char *result_filename)
{
    if (fs == nullptr || !fs->running())
        return nullptr;

    // Generate a 'random' filename by using timer ticks
    uint32_t ms = micros();

    char buff[9];
    char *fname;
    if (result_filename != nullptr)
        fname = result_filename;
    else
        fname = buff;

    snprintf(fname, 9, "%08u", (unsigned)ms);
    return fs->file_open(fname, "wb+");
}

void SystemManager::delete_tempfile(FileSystem *fs, const char *filename)
{
    if (fs == nullptr || !fs->running())
        return;

    fs->remove(filename);
}

/*
 Remove specified temporary file, if fnSDFAT available, then file is deleted there,
 otherwise deleted from FLASH
*/
void SystemManager::delete_tempfile(const char *filename)
{
    if (fnSDFAT.running())
        delete_tempfile(&fnSDFAT, filename);
    else
        delete_tempfile(&fsFlash, filename);
}

/*
 Create temporary file. fnSDFAT will be used if available, otherwise fsFlash.
 Filename will be 8 characters long. If provided, generated filename will be placed in result_filename
 File opened in "wb+" mode.
*/
FILE *SystemManager::make_tempfile(char *result_filename)
{
    if (fnSDFAT.running())
        return make_tempfile(&fnSDFAT, result_filename);
    else
        return make_tempfile(&fsFlash, result_filename);
}

// Copy file from source filesystem/filename to destination filesystem/name using optional buffer_hint for buffer size
size_t SystemManager::copy_file(FileSystem *source_fs, const char *source_filename, FileSystem *dest_fs, const char *dest_filename, size_t buffer_hint)
{
    Debug_printf("copy_file \"%s\" -> \"%s\"\r\n", source_filename, dest_filename);

    FILE *fin = source_fs->file_open(source_filename);
    if (fin == nullptr)
    {
        Debug_println("copy_file failed to open source");
        return 0;
    }
    uint8_t *buffer = (uint8_t *)malloc(buffer_hint);
    if (buffer == NULL)
    {
        Debug_println("copy_file failed to allocate copy buffer");
        fclose(fin);
        return 0;
    }

    size_t result = 0;
    FILE *fout = dest_fs->file_open(dest_filename, "wb");
    if (fout == nullptr)
    {
        Debug_println("copy_file failed to open destination");
    }
    else
    {
        size_t count = 0;
        do
        {
            count = fread(buffer, 1, buffer_hint, fin);
            result += fwrite(buffer, 1, count, fout);
        } while (count > 0);

        fclose(fout);
    }

    fclose(fin);
    free(buffer);

    Debug_printf("copy_file copied %u bytes\r\n", (unsigned)result);

    return result;
}

// From esp32-hal-dac.c
/*
void IRAM_ATTR SystemManager::dac_write(uint8_t pin, uint8_t value)
{
    if(pin != DAC_CHANNEL_1_GPIO_NUM && pin != DAC_CHANNEL_2_GPIO_NUM)
        return; // Not a DAC pin

    dac_channel_t dac_chan = pin == DAC_CHANNEL_1_GPIO_NUM ? DAC_CHANNEL_1 : DAC_CHANNEL_2;

    ESP_ERROR_CHECK(dac_output_enable(dac_chan));

    // Disable tone
    CLEAR_PERI_REG_MASK(SENS_SAR_DAC_CTRL1_REG, SENS_SW_TONE_EN);

    if (dac_chan == DAC_CHANNEL_2)
    {
        // Disable channel tone
        CLEAR_PERI_REG_MASK(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_CW_EN2_M);
        // Set the DAC value
        SET_PERI_REG_BITS(RTC_IO_PAD_DAC2_REG, RTC_IO_PDAC2_DAC, value, RTC_IO_PDAC2_DAC_S);   //dac_output
        // Channel output enable
        SET_PERI_REG_MASK(RTC_IO_PAD_DAC2_REG, RTC_IO_PDAC2_XPD_DAC | RTC_IO_PDAC2_DAC_XPD_FORCE);
    }
    else
    {
        // Disable Channel tone
        CLEAR_PERI_REG_MASK(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_CW_EN1_M);
        // Set the DAC value
        SET_PERI_REG_BITS(RTC_IO_PAD_DAC1_REG, RTC_IO_PDAC1_DAC, value, RTC_IO_PDAC1_DAC_S);   //dac_output
        // Channel output enable
        SET_PERI_REG_MASK(RTC_IO_PAD_DAC1_REG, RTC_IO_PDAC1_XPD_DAC | RTC_IO_PDAC1_DAC_XPD_FORCE);
    }
}
*/
/*
esp_err_t SystemManager::dac_output_disable(dac_channel_t channel)
{
    return ::dac_output_disable((::dac_channel_t)channel);
}
esp_err_t SystemManager::dac_output_enable(dac_channel_t channel)
{
    return ::dac_output_enable((::dac_channel_t)channel);
}
esp_err_t SystemManager::dac_output_voltage(dac_channel_t channel, uint8_t dac_value)
{
    return ::dac_output_voltage((::dac_channel_t)channel, dac_value);
}
*/

uint32_t SystemManager::get_psram_size()
{
#ifdef ESP_PLATFORM
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    return info.total_free_bytes + info.total_allocated_bytes;
#else
    return 0;
#endif
}

/*
    If buffer is NULL, simply returns size of file. Otherwise
    allocates buffer for reading file contents. Buffer must be managed by caller.
*/
int SystemManager::load_firmware(const char *filename, uint8_t *buffer)
{
    Debug_printf("load_firmware '%s'\r\n", filename);

    if (fsFlash.exists(filename) == false)
    {
        Debug_println("load_firmware FILE NOT FOUND");
        return -1;
    }

    FILE *f = fsFlash.file_open(filename);
    size_t file_size = FileSystem::filesize(f);

    Debug_printf("load_firmware file size = %u\r\n", (unsigned)file_size);

    if (buffer == NULL)
    {
        fclose(f);
        return file_size;
    }

    int bytes_read = -1;
    if (buffer == NULL)
    {
        Debug_println("load_firmware passed in buffer was NULL");
    }
    else
    {
        bytes_read = fread(buffer, 1, file_size, f);
    }

    fclose(f);
    return bytes_read;
}

bool SystemManager::has_button_c()
{
#ifdef ESP_PLATFORM
    if(safe_reset_gpio == GPIO_NUM_NC)
        return false;
    else
        return true;
#else
    // !ESP_PLATFORM
    return true;
#endif
}

// Return a string with the detected hardware version
const char *SystemManager::get_hardware_ver_str()
{
    if (_hardware_version == 0)
        check_hardware_ver(); // check it and see, I've got a fever of 103

    switch (_hardware_version)
    {
    /* RS232 */
    case 1 :
        return "RS232 Prototype";
        break;
    case 2 :
        return "RS232 Rev1 ESP32S3";
        break;
    case -1:
        return "fujinet-pc";
        break;
    case 0:
    default:
        return "Unknown";
        break;
    }
}

/* Find the FujiNet hardware version by checking the
   Pull-Up resistors per platform
*/
void SystemManager::check_hardware_ver()
{
#ifdef ESP_PLATFORM

    /* RS232
    */
#ifdef PIN_BUTTON_C
#if CONFIG_IDF_TARGET_ESP32S3
    _hardware_version = 2;
    safe_reset_gpio = PIN_BUTTON_C;
    setup_card_detect((gpio_num_t)PIN_CARD_DETECT); // enable SD card detect
#else
    _hardware_version = 1;
    safe_reset_gpio = PIN_BUTTON_C;
    setup_card_detect((gpio_num_t)PIN_CARD_DETECT); // enable SD card detect
#endif // CONFIG_IDF_TARGET_ESP32S3
#else
    safe_reset_gpio = GPIO_NUM_NC;
#endif // PIN_BUTTON_C

#else
    /* FujiNet-PC */
    _hardware_version = -1;
#endif /* ESP_PLATFORM end */
}

// Dumps list of current tasks
void SystemManager::debug_print_tasks()
{
#ifdef DEBUG
#ifdef ESP_PLATFORM

    static const char *status[] = {"Running", "Ready", "Blocked", "Suspened", "Deleted"};

    uint32_t n = uxTaskGetNumberOfTasks();
    TaskStatus_t *pTasks = (TaskStatus_t *)malloc(sizeof(TaskStatus_t) * n);
    n = uxTaskGetSystemState(pTasks, n, nullptr);

    for (int i = 0; i < n; i++)
    {
        // Debug_printf("T%02d %p c%c (%2d,%2d) %4dh %10dr %8s: %s\r\n",
        Debug_printf("T%02d %p (%2d,%2d) %4luh %10lur %8s: %s\r\n",
                     i + 1,
                     pTasks[i].xHandle,
                     //pTasks[i].xCoreID == tskNO_AFFINITY ? '_' : ('0' + pTasks[i].xCoreID),
                     pTasks[i].uxBasePriority, pTasks[i].uxCurrentPriority,
                     pTasks[i].usStackHighWaterMark,
                     pTasks[i].ulRunTimeCounter,
                     status[pTasks[i].eCurrentState],
                     pTasks[i].pcTaskName);
    }
    Debug_printf("\nCPU MHz: %lu\r\n", fnSystem.get_cpu_frequency());
#endif // ESP_PLATFORM
#endif // DEBUG
}
