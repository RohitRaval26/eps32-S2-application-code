#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include <sprofiler.h>
#include "esp_log.h"
#include "esp_debug_helpers.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include <stdbool.h>

#define NUM_OF_SPIN_TASKS   6
#define SPIN_ITER           500000  //Actual CPU cycles used will depend on compiler optimization
#define SPIN_TASK_PRIO      2
#define STATS_TASK_PRIO     3
#define STATS_TICKS         pdMS_TO_TICKS(1000)
#define ARRAY_SIZE_OFFSET   5   //Increase this if print_real_time_stats returns ESP_ERR_INVALID_SIZE
#define WIFI_SSID "V_202"
#define WIFI_PASS "NDRMK@V202"
static char task_names[NUM_OF_SPIN_TASKS][configMAX_TASK_NAME_LEN];
static SemaphoreHandle_t sync_spin_task;
static SemaphoreHandle_t sync_stats_task;

/**
 * @brief   Function to print the CPU usage of tasks over a given duration.
 *
 * This function will measure and print the CPU usage of tasks over a specified
 * number of ticks (i.e. real time stats). This is implemented by simply calling
 * uxTaskGetSystemState() twice separated by a delay, then calculating the
 * differences of task run times before and after the delay.
 *
 * @note    If any tasks are added or removed during the delay, the stats of
 *          those tasks will not be printed.
 * @note    This function should be called from a high priority task to minimize
 *          inaccuracies with delays.
 * @note    When running in dual core mode, each core will correspond to 50% of
 *          the run time.
 *
 * @param   xTicksToWait    Period of stats measurement
 *
 * @return
 *  - ESP_OK                Success
 *  - ESP_ERR_NO_MEM        Insufficient memory to allocated internal arrays
 *  - ESP_ERR_INVALID_SIZE  Insufficient array size for uxTaskGetSystemState. Trying increasing ARRAY_SIZE_OFFSET
 *  - ESP_ERR_INVALID_STATE Delay duration too short
 */








static esp_err_t print_real_time_stats(TickType_t xTicksToWait)
{
    TaskStatus_t *start_array = NULL, *end_array = NULL;
    UBaseType_t start_array_size, end_array_size;
    uint32_t start_run_time, end_run_time;
    esp_err_t ret;

    //Allocate array to store current task states
    start_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    start_array = malloc(sizeof(TaskStatus_t) * start_array_size);
    if (start_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }
    //Get current task states
    start_array_size = uxTaskGetSystemState(start_array, start_array_size, &start_run_time);
    if (start_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit;
    }

    vTaskDelay(xTicksToWait);

    //Allocate array to store tasks states post delay
    end_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    end_array = malloc(sizeof(TaskStatus_t) * end_array_size);
    if (end_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }
    //Get post delay task states
    end_array_size = uxTaskGetSystemState(end_array, end_array_size, &end_run_time);
    if (end_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit;
    }

    //Calculate total_elapsed_time in units of run time stats clock period.
    uint32_t total_elapsed_time = (end_run_time - start_run_time);
    if (total_elapsed_time == 0) {
        ret = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    printf("| Task | Run Time | Percentage\n");
    //Match each task in start_array to those in the end_array
    for (int i = 0; i < start_array_size; i++) {
        int k = -1;
        for (int j = 0; j < end_array_size; j++) {
            if (start_array[i].xHandle == end_array[j].xHandle) {
                k = j;
                //Mark that task have been matched by overwriting their handles
                start_array[i].xHandle = NULL;
                end_array[j].xHandle = NULL;
                break;
            }
        }
        //Check if matching task found
        if (k >= 0) {
            uint32_t task_elapsed_time = end_array[k].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
            uint32_t percentage_time = (task_elapsed_time * 100UL) / (total_elapsed_time * portNUM_PROCESSORS);
            printf("| %s | %"PRIu32" | %"PRIu32"%%\n", start_array[i].pcTaskName, task_elapsed_time, percentage_time);
        }
    }

    //Print unmatched tasks
    for (int i = 0; i < start_array_size; i++) {
        if (start_array[i].xHandle != NULL) {
            printf("| %s | Deleted\n", start_array[i].pcTaskName);
        }
    }
    for (int i = 0; i < end_array_size; i++) {
        if (end_array[i].xHandle != NULL) {
            printf("| %s | Created\n", end_array[i].pcTaskName);
        }
    }
    ret = ESP_OK;

exit:    //Common return path
    free(start_array);
    free(end_array);
    return ret;
}

static void spin_task(void *arg)
{
    xSemaphoreTake(sync_spin_task, portMAX_DELAY);
    while (1) {
        //Consume CPU cycles
        for (int i = 0; i < SPIN_ITER; i++) {
            __asm__ __volatile__("NOP");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void spin_task1(void * arg){
    spin_task(arg);
}

static void spin_task2(void * arg){
    spin_task(arg);
}

static void spin_task3(void * arg){
    spin_task(arg);
}

static void spin_task4(void * arg){
    spin_task(arg);
}

static void spin_task5(void * arg){
    spin_task(arg);
}

static void spin_task6(void * arg){
    spin_task(arg);
}


static void stats_task(void *arg)
{
    xSemaphoreTake(sync_stats_task, portMAX_DELAY);

    //Start all the spin tasks
    for (int i = 0; i < NUM_OF_SPIN_TASKS; i++) {
        xSemaphoreGive(sync_spin_task);
    }

    //Print real time stats periodically
    while (1) {
        printf("\n\nGetting real time stats over %"PRIu32" ticks\n", STATS_TICKS);
        if (print_real_time_stats(STATS_TICKS) == ESP_OK) {
            printf("Real time stats obtained\n");
        } else {
            printf("Error getting real time stats\n");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        printf("WiFi Connected\n");
        // Add your hello world functionality or any other application logic here
        printf("Hello world!\n");
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);
        printf("This is %s chip with %d CPU core(s).\n", CONFIG_IDF_TARGET, chip_info.cores);
    }
}


void hello_world_task(void *pvParameter) {
    printf("Hello World Task Running\n");
    // Call stack trace at sample point
    esp_backtrace_print(100);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    vTaskDelete(NULL);
}
void wifi_task(void *pvParameter) {
    // Start WiFi operations
    printf("WiFi Task Running\n");

    // Example point to print call stack trace
    esp_backtrace_print(100);

    // Wait and then delete task
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    vTaskDelete(NULL);
}

void wifi_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Wi-Fi initialization code
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
}





void app_main(void) {
    esp_err_t ret;
    
    // WiFi initialization
    wifi_init();

    // Start Hello World Task
    xTaskCreate(&hello_world_task, "hello_world_task", 4096, NULL, 5, NULL);

    // Start WiFi Task
    xTaskCreate(&wifi_task, "wifi_task", 4096, NULL, 5, NULL);

    // Call profiler at intervals to capture stack traces
    sprofiler_initialize(100);

    // Loop through and print the call stack at each sample
    int count = 0;
    while (1) {
        printf("Sample point %d\n", count);
        esp_backtrace_print(100);  // Capture call stack
        count++;
        vTaskDelay(pdMS_TO_TICKS(1000)); // Sample every second
    }




    // Initialize NVS
    ret = nvs_flash_init();
    if (ret != ESP_OK) {
        ESP_LOGE("NVS", "NVS Flash Init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Initialize the default network interface
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Initialize the WiFi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    printf("Connecting to WiFi...\n");

    // Allow other core to finish initialization
    vTaskDelay(pdMS_TO_TICKS(100));

    // Start the profiler
    sprofiler_initialize(100);

    // Create semaphores to synchronize
    sync_spin_task = xSemaphoreCreateCounting(NUM_OF_SPIN_TASKS, 0);
    sync_stats_task = xSemaphoreCreateBinary();

    // Create spin tasks
    xTaskCreatePinnedToCore(spin_task1, "spin1", 1024, NULL, SPIN_TASK_PRIO, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(spin_task2, "spin2", 1024, NULL, SPIN_TASK_PRIO, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(spin_task3, "spin3", 1024, NULL, SPIN_TASK_PRIO, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(spin_task4, "spin4", 1024, NULL, SPIN_TASK_PRIO, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(spin_task5, "spin5", 1024, NULL, SPIN_TASK_PRIO, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(spin_task6, "spin6", 1024, NULL, SPIN_TASK_PRIO, NULL, tskNO_AFFINITY);

    // Create and start stats task
    xTaskCreatePinnedToCore(stats_task, "stats", 4096, NULL, STATS_TASK_PRIO, NULL, tskNO_AFFINITY);
    xSemaphoreGive(sync_stats_task);

    // Optional: Add a routine to handle a restart or shut down based on specific conditions
    printf("Restarting in 10 seconds...\n");
    for (int i = 10; i >= 0; i--) {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    esp_restart();
}
