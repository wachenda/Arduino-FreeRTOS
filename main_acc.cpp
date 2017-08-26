#include <Arduino_FreeRTOS.h>
#include <LiquidCrystal.h>
#include <arduino.h>
#include <task.h>
#include <timers.h>
#include <semphr.h>  // add the FreeRTOS functions for Semaphores (or Flags).

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

#define ALM_TRG_SHIFT   ( 23UL )
#define STARTUP_SHIFT   ( 22UL )
#define STATE_SHIFT     ( 20UL )
#define ALM_EN_SHIFT    ( 19UL )
#define FLD_SHIFT       ( 17UL )

enum ALM_TRG
{
    ALM_TRG_OFF  =   ( 0UL<<ALM_TRG_SHIFT ),
    ALM_TRG_ON   =   ( 1UL<<ALM_TRG_SHIFT )   
}alm_trg;

enum STATE 
{
    NORMAL      =   ( 0UL<<STATE_SHIFT ), 
    TIME_SET    =   ( 1UL<<STATE_SHIFT ), 
    ALARM_SET   =   ( 2UL<<STATE_SHIFT )
} State;

enum ACTIVE_FIELD 
{
    ACT_SEC     =   ( 0UL<<FLD_SHIFT ),
    ACT_HH      =   ( 1UL<<FLD_SHIFT ),
    ACT_MM      =   ( 2UL<<FLD_SHIFT ),
    ACT_ALM     =   ( 3UL<<FLD_SHIFT )    
} act_fld;

enum ALM_EN
{
    ALM_EN_OFF  =   ( 0UL<<ALM_EN_SHIFT ),
    ALM_EN_ON   =   ( 1UL<<ALM_EN_SHIFT )   
}alm_en;

#define ALM_TRG_MASK    ((uint32_t) ( 1UL<<ALM_TRG_SHIFT ))
#define STARTUP_MASK    ((uint32_t) ( 1UL<<STARTUP_SHIFT ))
#define STATE_MASK      ((uint32_t) ( 3UL<<STATE_SHIFT ))
#define ALM_EN_MASK     ((uint32_t) ( 1UL<<ALM_EN_SHIFT ))
#define FLD_MASK        ((uint32_t) ( 3UL<<FLD_SHIFT ))
#define TIME_MASK       ((uint32_t) 0x0001FFFF)



enum MERIDIAN {AM, PM};


/* Priorities at which the tasks are created. */
#define tskReadInput_PRIORITY       ( tskIDLE_PRIORITY + 1 )
#define tskReadTime_PRIORITY        ( tskIDLE_PRIORITY + 2 )
#define tskController_PRIORITY      ( tskIDLE_PRIORITY + 4 )
#define tskWriteDC_PRIORITY         ( tskIDLE_PRIORITY + 3 )

/* The rate at which time-tick is sent from ReadTime to Controller.
The times are converted from milliseconds to ticks using the pdMS_TO_TICKS() macro. */
#define ReadTime_FREQUENCY_MS   pdMS_TO_TICKS( 1000UL )  // Send Tick every 1000 mS

/* The rate at which keyboard input is checked by ReadLPC.
The times are converted from milliseconds to ticks using the pdMS_TO_TICKS() macro. */
#define ReadInput_FREQUENCY_MS    pdMS_TO_TICKS( 50UL )

/* The number of items the queue can hold at once. */
#define QUEUE_LENGTH    ( 6 )

/* Number of cycles to debounce switches in tskReadInput() */
#define DEBOUNCE ( 2 )

/* The values sent to the queueController. */
#define btnRIGHT    ( 0UL )
#define btnUP       ( 1UL )
#define btnDOWN     ( 2UL )
#define btnLEFT     ( 3UL )
#define btnSELECT   ( 4UL )
#define btnNONE     ( 5UL )
#define TIME_TICK   ( 6UL )


/*-----------------------------------------------------------*/

/*
 * The tasks as described in the Decomposition.pdf file.
 */
static void tskReadInput   (void *pvParameters);
static void tskReadTime  (void *pvParameters);
static void tskController(void *pvParameters);
static void tskWriteDC   (void *pvParameters);
//static TaskHandle_t htskWriteLC;

/*-----------------------------------------------------------*/

/* The queue used by Controller and WriteLC tasks. */
static QueueHandle_t queueWriteDC = NULL;
/* The queue used by ReadLPC, ReadTime, and Controller tasks. */
static QueueHandle_t queueController = NULL;

void main_acc(void)
{
    Serial.println();
    Serial.println();
    Serial.println("Starting ACC");
    
    /* Create the queues. */
    queueWriteDC    = xQueueCreate(QUEUE_LENGTH, sizeof(uint32_t));
    queueController = xQueueCreate(QUEUE_LENGTH, sizeof(uint32_t));

 
    lcd.begin(16, 2);              // start the library
    lcd.setCursor(0,0);
    lcd.print("Push the buttons"); // print a simple message
  

//    if ((queueController != NULL))
    if (( queueWriteDC != NULL ) && (queueController != NULL))
    {
        /* Create the tasks. */
        xTaskCreate(tskReadInput,                     /* The function that implements the task. */
                    "ReadInput",                      /* The text name assigned to the task - for debug only as it is not used by the kernel. */
                    configMINIMAL_STACK_SIZE+5,       /* The size of the stack to allocate to the task. */
                    NULL,                           /* The parameter passed to the task - not used in this simple case. */
                    tskReadInput_PRIORITY,            /* The priority assigned to the task. */
                    NULL );                         /* The task handle is not required, so NULL is passed. */

        xTaskCreate(tskReadTime, "ReadTime", configMINIMAL_STACK_SIZE+5, NULL, 
                    tskReadTime_PRIORITY, NULL );
                    
        xTaskCreate(tskController, "Controller", configMINIMAL_STACK_SIZE+15, NULL,
                    tskController_PRIORITY, NULL);
        
//        xTaskCreate(tskWriteDC, "WriteLC", configMINIMAL_STACK_SIZE+60, NULL,
//                    tskWriteDC_PRIORITY, NULL);
        xTaskCreate(tskWriteDC, "WriteLC", configMINIMAL_STACK_SIZE+30, NULL,
                    tskWriteDC_PRIORITY, NULL);
    }

  // Now the Task scheduler, which takes over control of scheduling individual Tasks, is automatically started.
  // No need to call vTaskStartScheduler();
}


static void tskReadInput(void *pvParameters)
{
    TickType_t xNextWakeTime;
    const TickType_t xBlockTime = ReadInput_FREQUENCY_MS;
    uint32_t ulReceivedValue = btnNONE;
    uint32_t ulQueuedValue = btnNONE;
    uint32_t ulLastValue = btnNONE;

    int val;
    uint8_t cnt = 0;
    uint32_t curBtn;
    uint32_t lastBtn = btnNONE;

    /* Prevent the compiler warning about the unused parameter. */
    (void)pvParameters;

    /* Initialise xNextWakeTime - this only needs to be done once. */
    xNextWakeTime = xTaskGetTickCount();

    for (;; )
    {

        /* Check Key Input */
        val = analogRead(0);      // read the value from the sensor
        if (val >= 652)  curBtn =            btnNONE; // We make this the 1st option for speed reasons since it will be the most likely result
        if (val < 38)    curBtn =            btnRIGHT; 
        if (val < 136 && val >= 38)   curBtn = btnUP;
        if (val < 259 && val >= 136)  curBtn = btnDOWN;
        if (val < 410 && val >= 259)  curBtn = btnLEFT;
        if (val < 652 && val >= 410)  curBtn = btnSELECT;  

        /* Debounce Key Input */
        if(curBtn == lastBtn)
        {
            cnt++;        
            if(cnt >= DEBOUNCE)
            {
                ulQueuedValue = curBtn;
                cnt = 0;
            }
        }
        lastBtn = curBtn;

        /* Send Key Input to Queue if New Key Pressed */
        if(ulQueuedValue != ulLastValue)
        {
            Serial.print("ReadInput:  ");
            switch(ulQueuedValue)
            {
                case btnNONE:
                    Serial.println("NONE");
                    break;
                case btnRIGHT:
                    Serial.println("RIGHT");
                    break;
                case btnUP:
                    Serial.println("UP");
                    break;
                case btnDOWN:
                    Serial.println("DOWN");
                    break;
                case btnLEFT:
                    Serial.println("LEFT");
                    break;
                case btnSELECT:
                    Serial.println("SELECT");
                    break;
            }
            /* queue input. */
            xQueueSend(queueController, &ulQueuedValue, 0U);
            ulLastValue = ulQueuedValue;
        }            
    }
        
    /* Place this task in the blocked state until it is time to run again.
    While in the Blocked state this task will not consume any CPU time. */
    vTaskDelayUntil(&xNextWakeTime, xBlockTime);
}

/*-----------------------------------------------------------*/

static void tskReadTime(void *pvParameters)
{
    TickType_t xNextWakeTime;
    const TickType_t xBlockTime = ReadTime_FREQUENCY_MS;
    uint32_t ulQueuedValue;

    /* Prevent the compiler warning about the unused parameter. */
    (void)pvParameters;

    /* Initialise xNextWakeTime - this only needs to be done once. */
    xNextWakeTime = xTaskGetTickCount();

    for (;; )
    {
        /* queue time-tick. */
        Serial.println("ReadTime: Produce TICK");
        ulQueuedValue = TIME_TICK;
        xQueueSend(queueController, &ulQueuedValue, 0U);

        /* Place this task in the blocked state until it is time to run again.
        While in the Blocked state this task will not consume any CPU time. */
        vTaskDelayUntil(&xNextWakeTime, xBlockTime);
    }
}

/*-----------------------------------------------------------*/

static void tskController(void *pvParameters)
{
    uint32_t ulReceivedValue;
    static uint32_t ulQueuedValue;

    static uint32_t NTime, STime, ATime;
    uint32_t hr;
    uint32_t mn;

    // Initialize flags
    alm_trg = ALM_TRG_OFF; 
    alm_en = ALM_EN_OFF;
    
    act_fld = ACT_HH;
    State = TIME_SET;
    
    bool bNEWSTATE = true;
    bool bStartUp = true;

    ulReceivedValue = btnNONE;

    /* Prevent the compiler warning about the unused parameter. */
    (void)pvParameters;

    for (;; )
    {

        /* Wait until something arrives in the queue - this task will block indefinitely.
        It will not use any CPU time while it is in the Blocked state. */
        xQueueReceive(queueController, &ulReceivedValue, portMAX_DELAY);

        Serial.print("Controller:  ");
        switch(ulReceivedValue)
        {
            case btnNONE:
                Serial.println("NONE");
                break;
            case btnRIGHT:
                Serial.println("RIGHT");
                break;
            case btnUP:
                Serial.println("UP");
                break;
            case btnDOWN:
                Serial.println("DOWN");
                break;
            case btnLEFT:
                Serial.println("LEFT");
                break;
            case btnSELECT:
                Serial.println("SELECT");
                break;
            
            
        }



        /* State Machine */
        switch(State)
        {
            case NORMAL:
                if(bNEWSTATE)
                {
                    Serial.println("NORMAL State");
                    bNEWSTATE = false;

                    act_fld = ACT_SEC;
                    ulQueuedValue &= ~(FLD_MASK);
                    ulQueuedValue |= act_fld;

                    // Change State
                    ulQueuedValue &= ~(STATE_MASK);
                    ulQueuedValue |= State;
//                    
                    // Clear and Re-set active field
                    act_fld = ACT_SEC;
                    ulQueuedValue &= ~(FLD_MASK);
                    ulQueuedValue != act_fld;
                    
                    // Clear and Replace STime
                    ulQueuedValue &= ~(TIME_MASK);
                    ulQueuedValue |= NTime;
                    
                    Serial.print("Normal:  ");
                    Serial.println(ulQueuedValue);
                }
                switch(ulReceivedValue)
                {
                    case btnUP:
                        State = TIME_SET;
                        bNEWSTATE = true;
                        break;
                    case btnDOWN:
                        State = ALARM_SET;
                        bNEWSTATE = true;
                        break;
                    case TIME_TICK:
                        // Clear and Replace STime
                        ulQueuedValue &= ~(TIME_MASK);
                        ulQueuedValue |= NTime;
                        break;
                    
                }
                break;

            case TIME_SET:
                bool bUPDATE_TIME;
                if(bNEWSTATE)
                {
                    bNEWSTATE = false;
                    bUPDATE_TIME = false;
                    
                    act_fld = ACT_HH;
                    ulQueuedValue &= ~(FLD_MASK);
                    ulQueuedValue != act_fld;

                    // Change State
                    ulQueuedValue &= ~(STATE_MASK);
                    ulQueuedValue |= State;
                    
                    // Clear and Replace STime
                    ulQueuedValue &= ~(TIME_MASK);
                    ulQueuedValue |= STime;
                    
                    if(bStartUp)
                        ulQueuedValue |= STARTUP_MASK;
                    Serial.println(ulQueuedValue); 
                }
                
                switch(ulReceivedValue)
                {
                    case btnSELECT:
                        if(!bStartUp)
                        {
                            State = NORMAL;
                            bNEWSTATE = true;
                            if(bUPDATE_TIME)
                                NTime = STime;
                        }
                        break;

                    case btnLEFT:
                        if(act_fld == ACT_MM)
                            act_fld = ACT_HH;
                        break;
                    case btnRIGHT:
                        if(act_fld == ACT_HH)
                            act_fld = ACT_MM;
                        break;

                    case btnUP:
                        bStartUp = false;
                        bUPDATE_TIME = true;
                        // Clear StartUp Bit
                        ulQueuedValue &= ~(STARTUP_MASK);
                        switch(act_fld)
                        {
                            case ACT_HH:
                                Serial.println("Incrementing up hour");
                                STime += 3600UL;
                                if(STime >= 24UL*3600UL)
                                    STime -= 24UL*3600UL;
                                break;

                            case ACT_MM:
                                hr = (uint32_t)(STime/3600UL);
                                mn = (uint32_t)((STime - hr*3600UL)/60UL);
                                mn++;
                                if(mn>59UL)
                                    mn = 0UL;
                                STime = hr*3600UL+mn*60UL;
                                break;    
                        }
                        break;
                        
                    case btnDOWN:
                        bStartUp = false;
                        bUPDATE_TIME = true;
                        // Clear StartUp Bit
                        ulQueuedValue &= ~(STARTUP_MASK);
                        switch(act_fld)
                        {
                            case ACT_HH:
//                                Serial.print("Incrementing down hour "); Serial.println(STime);
                                if(STime<3600UL)
                                    STime += 23UL*3600UL;
                                else
                                    STime -= 3600UL;
                                break;

                            case ACT_MM:
                                hr = (uint32_t)(STime/3600UL);
                                mn = (uint32_t)((STime - hr*3600UL)/60UL);
                                if(mn==0UL)
                                    mn = 59UL;
                                else
                                    mn--;
                                STime = hr*3600UL+mn*60UL;
                                break;    
                        }  
                }
                ulQueuedValue &= ~(FLD_MASK);
                ulQueuedValue != act_fld;
                
                // Clear and Re-set active field
                ulQueuedValue &= ~(FLD_MASK);
                ulQueuedValue |= act_fld;
                
                // Clear and Replace STime
                ulQueuedValue &= ~(TIME_MASK);
                ulQueuedValue |= STime;
                break;

            case ALARM_SET:
                if(bNEWSTATE)
                {
                    Serial.println("ALARM_SET State");
                    bNEWSTATE = false;

                    ulQueuedValue &= ~(ALM_EN_MASK);
                    ulQueuedValue |= alm_en;
                    Serial.print("alm_en:  "); Serial.println(alm_en);

                    // Initialize active field to HH
                    act_fld = ACT_HH;
                    ulQueuedValue &= ~(FLD_MASK);
                    ulQueuedValue != act_fld;

                    // Change State
                    ulQueuedValue &= ~(STATE_MASK);
                    ulQueuedValue |= State;
                    
                    // Clear and Replace NTime with ATime
                    ulQueuedValue &= ~(TIME_MASK);
                    ulQueuedValue |= ATime;

                }
                switch(ulReceivedValue)
                {
                    case btnSELECT:
                        if(!bStartUp)
                        {
                            State = NORMAL;
                            bNEWSTATE = true;
                        }
                        break;
    
                    case btnLEFT:
                        if(act_fld == ACT_MM)
                            act_fld = ACT_HH;
                        else if(act_fld == ACT_ALM)
                            act_fld = ACT_MM;
                        break;
                        
                    case btnRIGHT:
                        if(act_fld == ACT_HH)
                            act_fld = ACT_MM;
                        else if(act_fld == ACT_MM)
                            act_fld = ACT_ALM;
                        break;
    
                    case btnUP:
                        switch(act_fld)
                        {
                            case ACT_HH:
//                                Serial.println("Incrementing up hour");
                                ATime += 3600UL;
                                if(ATime >= 24UL*3600UL)
                                    ATime -= 24UL*3600UL;
                                break;
    
                            case ACT_MM:
                                hr = (uint32_t)(ATime/3600UL);
                                mn = (uint32_t)((ATime - hr*3600UL)/60UL);
                                mn++;
                                if(mn>59UL)
                                    mn = 0UL;
                                ATime = hr*3600UL+mn*60UL;
                                break;    
                            case ACT_ALM:
                                alm_en = ALM_EN_ON;
//                                ulQueuedValue &= ~(ALM_EN_MASK);
//                                ulQueuedValue |= alm_en;
                                break;
                        }
                        break;
                        
                    case btnDOWN:
                        switch(act_fld)
                        {
                            case ACT_HH:
//                                Serial.print("Incrementing down hour "); Serial.println(ATime);
                                if(ATime<3600UL)
                                    ATime += 23UL*3600UL;
                                else
                                    ATime -= 3600UL;
                                break;
    
                            case ACT_MM:
                                hr = (uint32_t)(ATime/3600UL);
                                mn = (uint32_t)((ATime - hr*3600UL)/60UL);
                                if(mn==0UL)
                                    mn = 59UL;
                                else
                                    mn--;
                                ATime = hr*3600UL+mn*60UL;
                                break;   
                             case ACT_ALM:
                                alm_en = ALM_EN_OFF;
//                                ulQueuedValue &= ~(ALM_EN_MASK);
//                                ulQueuedValue |= alm_en;
                                break; 
                        }  
                }
                
                
                // Clear and Re-set alarm enable
                ulQueuedValue &= ~(ALM_EN_MASK);
                ulQueuedValue |= alm_en;
                
                // Clear and Re-set active field
                ulQueuedValue &= ~(FLD_MASK);
                ulQueuedValue |= act_fld;
                
                // Clear and Replace STime
                ulQueuedValue &= ~(TIME_MASK);
                ulQueuedValue |= ATime;
                break;
        }

        switch(ulReceivedValue)
        {

            case TIME_TICK:
                Serial.println("TICK");
                if(~bStartUp)
                {
                    if(NTime<24UL*3600UL)
                        NTime++;
                    else
                        NTime = 0UL;
                }
                /* queue input. */
                xQueueSend(queueWriteDC, &ulQueuedValue, 0U);
                break;
        }
    }
}

/*-----------------------------------------------------------*/

//byte smiley[8] = {
//  B00000,
//  B10001,
//  B00000,
//  B00000,
//  B10001,
//  B01110,
//  B00000,
//};
//
//byte upline[8] = {
//  B11111,
//  B00000,
//  B00000,
//  B00000,
//  B00000,
//  B00000,
//  B00000,
//};

static void tskWriteDC( void *pvParameters )
{
    uint32_t ulReceivedValue;
    uint32_t tm;
    uint32_t hh, mm;
    
    char *meridien;

    bool bFlash=false;
    

    /* Prevent the compiler warning about the unused parameter. */
    ( void ) pvParameters;
    for( ;; )
    {

        /* Wait until something arrives in the queue - this task will block indefinitely.
        It will not use any CPU time while it is in the Blocked state. */
        xQueueReceive(queueWriteDC, &ulReceivedValue, portMAX_DELAY);

        Serial.print("WriteDC:");
        Serial.print(ulReceivedValue); Serial.print(" ");
        tm = (uint32_t) ulReceivedValue&TIME_MASK;
        Serial.print(tm); Serial.print(" ");
        hh = (uint32_t) tm/3600UL;
        mm = (uint32_t) (tm-hh*3600UL)/60UL;

        Serial.print("Startup Mask:  "); Serial.println(ulReceivedValue&STARTUP_MASK);
//        uint32_t curstate = ulReceivedValue&STATE_MASK;
//        uint32_t curfield = ulReceivedValue&FLD_MASK;
//        uint32_t cur_alrm_en = ulReceivedValue&ALM_EN_MASK;
        uint32_t cur_alrm_trg = ulReceivedValue&ALM_TRG_MASK;
//        Serial.print("State Mask:  "); Serial.println((ulReceivedValue&STATE_MASK));
//        Serial.print("Field Mask:  "); Serial.println((ulReceivedValue&FLD_MASK));
//        Serial.print("ALM_EN Mask:  "); Serial.println((ulReceivedValue&ALM_EN_MASK));
        Serial.print("ALM_TRG Mask:  "); Serial.println(cur_alrm_trg);
        
      
        if(hh<12UL)
            meridien = "AM";
        else
            meridien = "PM";
        if(!(ulReceivedValue&STARTUP_MASK) && (hh==0UL))
            hh = 12UL;
        else if(hh>=13)
            hh -= 12;
            
        Serial.print(hh); Serial.print(":");
        if(mm<10UL) Serial.print("0");
        Serial.print(mm); Serial.print(" ");
        Serial.print(meridien);
        Serial.println();


        lcd.clear();
        lcd.setCursor(2,0);
        if(hh<10UL)
            lcd.print(" ");

         bFlash = !bFlash;


//        switch(curstate)
        switch(ulReceivedValue&STATE_MASK)
        {
            case NORMAL:
                lcd.print(hh);
                
               
                Serial.print("bFlash: "); Serial.println(bFlash);
                if(bFlash)
                    lcd.print(":");
                else
                    lcd.print(" ");
                
                if(mm<10)
                    lcd.print("0");
                lcd.print(mm);
                lcd.print(" ");
                lcd.print(meridien);
                if((ulReceivedValue&ALM_EN_MASK)==ALM_EN_ON)
                    lcd.print("   ALM");

            break;

            case TIME_SET:

//                if(curfield == ACT_HH)
                if((ulReceivedValue&FLD_MASK) == ACT_HH)
                {
                    if(bFlash)
                        lcd.print(hh);
                    else
                        if(hh<10UL)
                            lcd.print(" ");
                        else
                            lcd.print("  ");
                }
                else
                    lcd.print(hh);
                
                lcd.print(":");

//                if(curfield == ACT_MM)
                if((ulReceivedValue&FLD_MASK) == ACT_MM)
                {
                    if(bFlash)
                    {
                        if(mm<10)
                            lcd.print("0");
                        lcd.print(mm);
                    }
                    else   
                        lcd.print("  ");
                }
                else
                {
                    if(mm<10)
                        lcd.print("0");
                    lcd.print(mm);
                }
                lcd.print(" ");
                if((ulReceivedValue&FLD_MASK) == ACT_HH)
                {
                    if(bFlash)
                        lcd.print(meridien);
                    else
                        lcd.print("  ");
                }
                else
                    lcd.print(meridien);

                lcd.setCursor(2,1);
                lcd.print("Set Time");

            break;

            case ALARM_SET:
                if((ulReceivedValue&FLD_MASK) == ACT_HH)
                {
                    if(bFlash)
                        lcd.print(hh);
                    else
                        if(hh<10UL)
                            lcd.print(" ");
                        else
                            lcd.print("  ");
                }
                else
                    lcd.print(hh);
                
                lcd.print(":");

                if((ulReceivedValue&FLD_MASK) == ACT_MM)
                {
                    if(bFlash)
                    {
                        if(mm<10)
                            lcd.print("0");
                        lcd.print(mm);
                    }
                    else   
                        lcd.print("  ");
                }
                else
                {
                    if(mm<10)
                        lcd.print("0");
                    lcd.print(mm);
                }
                lcd.print(" ");
                if((ulReceivedValue&FLD_MASK) == ACT_HH)
                {
                    if(bFlash)
                        lcd.print(meridien);
                    else
                        lcd.print("  ");
                }
                else
                    lcd.print(meridien);

//                lcd.print(" ");

                if((ulReceivedValue&FLD_MASK)==ACT_ALM)
                {
                    
                    if(bFlash)
                    {
                        if((ulReceivedValue&ALM_EN_MASK)==ALM_EN_ON)
                            lcd.print(" ON ");
                        else
                            lcd.print(" OFF");
                    }
                    else
                        lcd.print("   ");
                }
                else
                {
                    if((ulReceivedValue&ALM_EN_MASK)==ALM_EN_ON)
                        lcd.print(" ON ");
                    else
                        lcd.print(" OFF");
                }

             

//                lcd.setCursor(2,1);
//                lcd.print("Set Alarm");

            break;
        }

    
    }
}
/*-----------------------------------------------------------*/



