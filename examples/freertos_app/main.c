#include <stdio.h>
/* Mock FreeRTOS headers if not present, but for example we assume they exist in include path */
/* To make this compile in the CI environment without FreeRTOS, we will guard it or use a stub */
#if 0
#include "FreeRTOS.h"
#include "task.h"
#else
/* Stub for demonstration if FreeRTOS is not found */
#define taskENTER_CRITICAL()
#define taskEXIT_CRITICAL()
#define vTaskDelay(x)
#define pdMS_TO_TICKS(x) ((x)/10)
#endif

#include "iolinki/iolink.h"
#include "iolinki/platform.h"
#include "iolinki/phy.h"

/* Platform Override for Critical Sections */
void iolink_critical_enter(void) {
    taskENTER_CRITICAL();
}

void iolink_critical_exit(void) {
    taskEXIT_CRITICAL();
}

/* Mock PHY */
const iolink_phy_api_t g_phy_freertos = {
    .init = NULL,
    .set_baudrate = NULL,
    .send = NULL, // Implement actual UART send
    .recv_byte = NULL // Implement actual UART recv
};

/* IO-Link Stack Task */
void iolink_task_entry(void *pvParameters) {
    (void)pvParameters;
    
    /* Initialize Stack */
    iolink_init(&g_phy_freertos);
    
    printf("IO-Link Task Started\n");

    for (;;) {
        /* Process Stack */
        iolink_process();
        
        /* Yield / Sleep to allow other tasks (1ms cycle) */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* Application Task (simulating events trigger from another thread) */
void app_task_entry(void *pvParameters) {
    (void)pvParameters;
    
    for (;;) {
        /* Trigger an event every 5 seconds safely */
        iolink_event_trigger(NULL /* ctx */, 0x1800, IOLINK_EVENT_TYPE_NOTIFICATION);
        
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

int main(void) {
    printf("Starting FreeRTOS Example...\n");

    /* Create Tasks */
    /* xTaskCreate(iolink_task_entry, "IOLink", 1024, NULL, 2, NULL); */
    /* xTaskCreate(app_task_entry, "App", 1024, NULL, 1, NULL); */
    
    /* Start Scheduler */
    /* vTaskStartScheduler(); */
    
    return 0;
}
