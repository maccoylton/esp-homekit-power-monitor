/*
 * Copyright 2018 David B Brown (@maccoylton)
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *
 * Example of using esp-homekit library
 * to monitor power consumption using a 100A SCT-013-000
 *
 */

#define DEVICE_MANUFACTURER "maccoylton"
#define DEVICE_NAME "Power Monitor"
#define DEVICE_MODEL "1"
#define DEVICE_SERIAL "123456780"
#define FW_VERSION "1.0"

#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <espressif/esp_common.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <wifi_config.h>


#include <adv_button.h>
#include <led_codes.h>
#include <udplogger.h>
#include <custom_characteristics.h>
#include <shared_functions.h>
#include <esp-homekit-eve-history.h>

// add this section to make your device OTA capable
// create the extra characteristic &ota_trigger, at the end of the primary service (before the NULL)
// it can be used in Eve, which will show it, where Home does not
// and apply the four other parameters in the accessories_information section

#include <ota-api.h>

#define SAVE_DELAY 2000
#define POWER_MONITOR_POLL_PERIOD 3000 // 3 seconds * 1000
#define EVE_HISTORY_LOG_PERIOD 6000000 // 10 minutes * 1000
#define EVE_HISTORY_LOG_LOOPS 200 // EVE_HISTORY_LOG_PERIOD / POWER_MONITOR_POLL_PERIOD


homekit_characteristic_t wifi_check_interval   = HOMEKIT_CHARACTERISTIC_(CUSTOM_WIFI_CHECK_INTERVAL, 10, .setter=wifi_check_interval_set);
/* checks the wifi is connected and flashes status led to indicated connected */
homekit_characteristic_t task_stats   = HOMEKIT_CHARACTERISTIC_(CUSTOM_TASK_STATS, false , .setter=task_stats_set);
homekit_characteristic_t wifi_reset   = HOMEKIT_CHARACTERISTIC_(CUSTOM_WIFI_RESET, false, .setter=wifi_reset_set);
homekit_characteristic_t ota_beta     = HOMEKIT_CHARACTERISTIC_(CUSTOM_OTA_BETA, false, .setter=ota_beta_set);
homekit_characteristic_t lcm_beta    = HOMEKIT_CHARACTERISTIC_(CUSTOM_LCM_BETA, false, .setter=lcm_beta_set);

homekit_characteristic_t ota_trigger  = API_OTA_TRIGGER;
homekit_characteristic_t name         = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  DEVICE_MANUFACTURER);
homekit_characteristic_t serial       = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model        = HOMEKIT_CHARACTERISTIC_(MODEL,         DEVICE_MODEL);
homekit_characteristic_t revision     = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION,  FW_VERSION);

homekit_characteristic_t volts = HOMEKIT_CHARACTERISTIC_(CUSTOM_VOLTS, 0);
homekit_characteristic_t amps = HOMEKIT_CHARACTERISTIC_(CUSTOM_AMPS, 0);

homekit_characteristic_t watts = HOMEKIT_CHARACTERISTIC_(CUSTOM_WATTS, 0);
homekit_characteristic_t switch_on = HOMEKIT_CHARACTERISTIC_(ON, false);

homekit_characteristic_t EVE_history_status_S2R1_116 = HOMEKIT_CHARACTERISTIC_(CUSTOM_EVE_HISTORY_S2R1, 0, 0, .getter=getter_EVE_history_status_S2R1_116 );
homekit_characteristic_t EVE_history_entries_S2R2_117 = HOMEKIT_CHARACTERISTIC_(CUSTOM_EVE_HISTORY_S2R2, 0, 0, .getter=getter_EVE_history_entries_S2R2_117 );
homekit_characteristic_t EVE_history_request_S2W1_11C = HOMEKIT_CHARACTERISTIC_(CUSTOM_EVE_HISTORY_S2W1, .setter=setter_EVE_history_request_S2W1_11C);
homekit_characteristic_t EVE_set_time_S2W2_121 = HOMEKIT_CHARACTERISTIC_(CUSTOM_EVE_HISTORY_S2W2, .setter=setter_EVE_set_time_S2W2_121);



TaskHandle_t power_monitoring_task_handle;


int led_off_value=-1; /* global varibale to support LEDs set to 0 where the LED is connected to GND, 1 where +3.3v */

const int status_led_gpio = 2; /*set the gloabl variable for the led to be used for showing status */

// Power monitor items
double Calib = 3.27;
double RMSCurrent;
int RMSPower;
int LineVolts = 230;

eve_history_power_t power_log[EVE_HISTORY_MAX_LOG_ENTRIES];


void power_monitoring_task(void *_args) {

    static int Current = 0;
    static int MaxCurrent = 0;
    static int MinCurrent = 1023;
    static int PeakCurrent = 0;
    static int loop_count = EVE_HISTORY_LOG_LOOPS - 1; /*this wil ensure a log entry first tie round the loop */
    static int total_power_consumed=0;
    
    printf ("%s:\n", __func__);

    
    while (1)
    {

	Current = 0;
	MaxCurrent = 0;
	MinCurrent = 1023;
	PeakCurrent = 0;

        // Needs to sample for at least one and half mains cycles or > 30mS
        for (int j = 0 ; j <= 50 ; j++)
        {
            
            Current =  sdk_system_adc_read() ;   //Reads A/D input and records maximum and minimum current
            //printf (" %d", Current);
            if (Current >= MaxCurrent)
                MaxCurrent = Current;
            
            if (Current <= MinCurrent)
                MinCurrent = Current;

            vTaskDelay(1 / portTICK_PERIOD_MS);

        } // End of samples loop
        
        PeakCurrent = MaxCurrent - MinCurrent;
        printf ("%s: PeakCurrent: %d\n", __func__, PeakCurrent );
        
        RMSCurrent = (PeakCurrent * 0.3535) / Calib;
        RMSPower = LineVolts * RMSCurrent;
        amps.value = HOMEKIT_FLOAT (RMSCurrent); //Calculates RMS current based on maximum value and scales according to calibration
        watts.value = HOMEKIT_UINT16 (RMSPower );  //Calculates RMS Power Assuming Voltage 240VAC, change to 110VAC accordingly
        volts.value.int_value = LineVolts;
       
        
        printf("%s: [HLW] Current (A)         :%f2.2\n", __func__, RMSCurrent);
        printf("%s: [HLW] Power (VA) :%d\n", __func__, RMSPower);
        
        homekit_characteristic_bounds_check( &volts);
        homekit_characteristic_bounds_check( &amps);
        homekit_characteristic_bounds_check( &watts);
        
        homekit_characteristic_notify(&volts, volts.value);
        homekit_characteristic_notify(&amps, amps.value);
        homekit_characteristic_notify(&watts, watts.value);
        
        
        loop_count++;
        total_power_consumed += RMSPower;
        
        if ( loop_count == EVE_HISTORY_LOG_LOOPS) {
            /* we store eve history log ever 10 minutes */
            

            if(eve_history_log_header.entry_counter = EVE_HISTORY_MAX_LOG_ENTRIES) {
                /* if we have run out of space, for new log entries start again */
                eve_history_log_header.entry_counter = 0;
            }
            
            power_log[eve_history_log_header.entry_counter].power_times_10 = (total_power_consumed/EVE_HISTORY_LOG_LOOPS) *10;
            eve_history_log_header.entry_counter ++;
            printf ("%s: logging power, %d, log entries, %d\n", __func__, power_log[eve_history_log_header.entry_counter-1].power_times_10, eve_history_log_header.entry_counter);
            
            total_power_consumed = 0;
            loop_count = 0;
        }
        
        vTaskDelay(POWER_MONITOR_POLL_PERIOD / portTICK_PERIOD_MS);
    }
    
}


void gpio_init() {

}


homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_sensor, .services=(homekit_service_t*[]){
        HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]){
            &name,
            &manufacturer,
            &serial,
            &model,
            &revision,
            HOMEKIT_CHARACTERISTIC(IDENTIFY, identify),
            NULL
        }),
        

        HOMEKIT_SERVICE(SWITCH, .primary=true, .characteristics=(homekit_characteristic_t*[]){
            HOMEKIT_CHARACTERISTIC(NAME, "Switch"),
            &switch_on,
            &volts,
            &watts,
            &amps,
            &ota_trigger,
            &wifi_reset,
            &ota_beta,
            &lcm_beta,
            &task_stats,
            &wifi_check_interval,
            NULL
        }),
        
        HOMEKIT_SERVICE(EVE_HISTORY, .primary=true, .characteristics=(homekit_characteristic_t*[]){
            HOMEKIT_CHARACTERISTIC(NAME, "EVE HISTORY"),
            &EVE_history_status_S2R1_116,
            &EVE_history_entries_S2R2_117,
            &EVE_history_request_S2W1_11C,
            &EVE_set_time_S2W2_121,
            NULL
        }),

        NULL
    }),
    NULL
};

homekit_server_config_t config = {
    .accessories = accessories,
    .password = "111-11-111",
    .setupId = "1234",
    .on_event = on_homekit_event
};


void eve_history_send_log(uint32_t starting_from_address){
    
    
}


void recover_from_reset (int reason){
    /* called if we restarted abnormally */
    printf ("%s: reason %d\n", __func__, reason);
}

void save_characteristics ( ){
    
    printf ("%s:\n", __func__);
    save_characteristic_to_flash(&wifi_check_interval, wifi_check_interval.value);
}


void accessory_init_not_paired (void) {
    /* initalise anything you don't want started until wifi and homekit imitialisation is confirmed, but not paired */
    
}


void accessory_init (void ){
    /* initalise anything you don't want started until wifi and pairing is confirmed */

    load_characteristic_from_flash(&wifi_check_interval);

    /* initialise the eve history stuff*/
    time_t now;
    
    time (&now);
    
    eve_history_status.actual_time = (uint32_t)now;
    eve_history_status.negative_offset = 0 ;
    eve_history_status.reference_time = 0;
    eve_history_status.signature_size = 4;
    eve_history_status.signature = 0x0102020207020f03;
    eve_history_status.last_memory_position = 0;
    eve_history_status.unknown = 0;
    eve_history_status.fixed = 0x01ff;
    
    printf ("Size of history header %d, size of history status %d", sizeof ( eve_history_log_header) , sizeof (eve_history_status));
    
    xTaskCreate(power_monitoring_task, "Power Monitoring Task", 512, NULL, tskIDLE_PRIORITY+1, &power_monitoring_task_handle);

}


void user_init(void) {
    
    standard_init (&name, &manufacturer, &model, &serial, &revision);

    gpio_init();

    wifi_config_init(DEVICE_NAME, NULL, on_wifi_ready);
    
}
