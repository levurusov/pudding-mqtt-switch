#include <string.h>
#include <stdio.h>
#include <api_os.h>
#include <api_call.h>
#include <api_gps.h>
#include <api_event.h>
#include <api_hal_uart.h>
#include <api_debug.h>
#include "buffer.h"
#include "gps_parse.h"
#include "math.h"
#include "gps.h"
#include "api_hal_pm.h"
#include "api_mqtt.h"
#include "api_key.h"
#include "time.h"
#include "api_info.h"
#include "assert.h"
#include "api_socket.h"
#include "api_network.h"
#include "api_hal_gpio.h"
#include "api_hal_adc.h"
#include "api_hal_pm.h"
#include "api_hal_watchdog.h"

#include "mqtt_task.h"

#define MAIN_TASK_STACK_SIZE    (2048 * 4)
#define MAIN_TASK_PRIORITY      0
#define MAIN_TASK_NAME          "Main Task"

#define PDP_CONTEXT_APN         "internet.mts.ru"
#define PDP_CONTEXT_USERNAME    "mts"
#define PDP_CONTEXT_PASSWD      "mts"


static HANDLE mainTaskHandle = NULL;


bool AttachActivate()
{
    uint8_t status;
    bool ret = Network_GetAttachStatus(&status);

    if(!ret) {
        Trace(2, "Get attach status fail");
        return false;
    }

    Trace(2, "Attach status: %d", status);

    if(!status) {
        ret = Network_StartAttach();

        if(!ret) {
            Trace(2, "Network attach fail");
            return false;
        }
    } else {
        ret = Network_GetActiveStatus(&status);

        if(!ret) {
            Trace(2, "Get activate staus fail");
            return false;
        }
        
        Trace(2, "Activate status: %d",status);
        
        if(!status) {
            Network_PDP_Context_t context = {
                .apn = PDP_CONTEXT_APN,
                .userName = PDP_CONTEXT_USERNAME,
                .userPasswd = PDP_CONTEXT_PASSWD
            };
            Network_StartActive(context);
        }
    }

    return true;
}


void EventDispatch(API_Event_t* pEvent)
{
    switch(pEvent->id) {
        case API_EVENT_ID_NO_SIMCARD:
            Trace(10,"!!NO SIM CARD%d!!!!",pEvent->param1);
            break;

        case API_EVENT_ID_NETWORK_REGISTER_DENIED:
            Trace(2,"network register denied");

        case API_EVENT_ID_NETWORK_REGISTER_NO:
            Trace(2,"network register no");
            break;

        case API_EVENT_ID_NETWORK_REGISTERED_HOME:
        case API_EVENT_ID_NETWORK_REGISTERED_ROAMING:
        case API_EVENT_ID_NETWORK_DETACHED:
        case API_EVENT_ID_NETWORK_ATTACH_FAILED:
        case API_EVENT_ID_NETWORK_ATTACHED:
        case API_EVENT_ID_NETWORK_DEACTIVED:
        case API_EVENT_ID_NETWORK_ACTIVATE_FAILED:
            AttachActivate();
            break;

        case API_EVENT_ID_NETWORK_ACTIVATED:
            Trace(2,"network activate success");
            if(semMqttStart)
                OS_ReleaseSemaphore(semMqttStart);
            break;

        case API_EVENT_ID_SIGNAL_QUALITY:
            Trace(2,"CSQ: %d", pEvent->param1);
            break;

        default:
            break;
    }
}


/**
 * The Main task
 *
 * Init HW and run MQTT task. 
 *
 * @param  pData Parameter passed in when this function is called
 */
void app_MainTask(void *pData)
{
    API_Event_t* event=NULL;

    Trace(1, "Main Task started");

    //sync time from GSM/GPRS network when attach success
    TIME_SetIsAutoUpdateRtcTime(true);

    PM_PowerEnable(POWER_TYPE_VPAD, true); // GPIO0  ~ GPIO7  and GPIO25 ~ GPIO36    2.8V
    //PM_SetSysMinFreq(PM_SYS_FREQ_312M);

    // Create MQTT task
    MqttTaskInit();

    // Wait and process system events
    while(1) {
        if(OS_WaitEvent(mainTaskHandle, (void**)&event, OS_TIME_OUT_WAIT_FOREVER)) {
            EventDispatch(event);
            OS_Free(event->pParam1);
            OS_Free(event->pParam2);
            OS_Free(event);
        }
    }
}

/**
 * The entry point of application. Just create the main task.
 */
void app_Main(void)
{
    mainTaskHandle = OS_CreateTask(app_MainTask,
        NULL, NULL, MAIN_TASK_STACK_SIZE, MAIN_TASK_PRIORITY, 0, 0, MAIN_TASK_NAME);
    OS_SetUserMainHandle(&mainTaskHandle);
}

