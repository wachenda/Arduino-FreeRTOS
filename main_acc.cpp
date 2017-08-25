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

enum STATE 
{
    NORMAL      =   ( 0UL<<20 ), 
    TIME_SET    =   ( 1UL<<20 ), 
    ALARM_SET   =   ( 2UL<<20 )
} State;

enum ACTIVE_FIELD 
{
    ACT_SEC     =   ( 0UL<<17 ),
    ACT_HH      =   ( 1UL<<17 ),
    ACT_MM      =   ( 2UL<<17 ),
    ACT_ALM     =   ( 3UL<<17 )    
} act_fld;

enum ALM_EN
{
    ALM_EN_OFF  =   ( 0UL<<19 ),
    ALM_EN_ON   =   ( 1UL<<19 )   
}alm_en;


#define STARTUP_SHIFT   ( 22UL )
#define STATE_SHIFT     ( 20UL )
#define ALM_EN_SHIFT    ( 19UL )
#define FLD_SHIFT       ( 17UL )

#define STARTUP_MASK    ((uint32_t) ( 1UL<<STARTUP_SHIFT ))
#define STATE_MASK      ((uint32_t) ( 3UL<<STATE_SHIFT ))
#define ALM_EN_MASK     ((uint32_t) ( 1UL<<ALM_EN_SHIFT ))
#define FLD_MASK        ((uint32_t) ( 3UL<<FLD_SHIFT ))
#define TIME_MASK       ((uint32_t) 0x0001FFFF)



enum MERIDIAN {AM, PM};

typedef struct
{
    int hh;
    int mm;
    int sec;
    MERIDIAN meridien;
    enum alm {OFF, ON};
    enum fld {HOUR, MIN, ALARM};
    STATE state;
}STATETIME;



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
#define ReadInput_FREQUENCY_MS    pdMS_TO_TICKS( 25UL )

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

//
//#define LP_ON           ( 1UL )
//#define LP_OFF          ( 2UL )
//#define C_NORMAL        ( 3UL )
//#define C_FAILURE       ( 4UL )
//#define TIME_TICK       ( 5UL )

/* The values sent to the queueWriteLC. */
#define RL_ON           ( 1UL )
#define RL_OFF          ( 2UL )
#define GL_ON           ( 3UL )
#define GL_OFF          ( 4UL )
#define YL_ON           ( 5UL )
#define YL_OFF          ( 6UL )

/* SuperStates*/
#define LP_ON                   ( 1UL )
#define LP_OFF                  ( 2UL )
#define LP_ON_C_NORMAL          ( 3UL )
#define LP_ON_C_FAILURE         ( 4UL )
#define LP_ON_C_NORMAL_R_ON     ( 5UL )
#define LP_ON_C_NORMAL_G_ON     ( 6UL )
#define LP_ON_C_NORMAL_Y_ON     ( 7UL )
#define LP_ON_C_FAILURE_R_ON    ( 8UL )
#define LP_ON_C_FAILURE_R_OFF   ( 9UL )

#define DELAY_LP_ON_C_NORMAL_R_ON   ( 9UL )
#define DELAY_LP_ON_C_NORMAL_G_ON   ( 6UL )
#define DELAY_LP_ON_C_NORMAL_Y_ON   ( 3UL )
#define DELAY_LP_ON_C_FAILURE_R_ON  ( 2UL )
#define DELAY_LP_ON_C_FAILURE_R_OFF ( 2UL )

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


static inline char *stringFromMeridien(enum MERIDIAN f)
{
    static const char *strings[] = { " AM", " PM" };

    return strings[f];
}

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

        xTaskCreate(tskReadTime, "ReadTime", configMINIMAL_STACK_SIZE, NULL, 
                    tskReadTime_PRIORITY, NULL );
                    
        xTaskCreate(tskController, "Controller", configMINIMAL_STACK_SIZE+10, NULL,
                    tskController_PRIORITY, NULL);
        
        xTaskCreate(tskWriteDC, "WriteLC", configMINIMAL_STACK_SIZE+20, NULL,
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
//        Serial.print(millis());
//        Serial.print("\t  ");
//        Serial.println(val);

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
    uint32_t ulQueuedValue = 0;

    

    static STATETIME NormalTime, Time_SetTime, Alarm_SetTime;
    NormalTime.state = NORMAL;
    Time_SetTime.state = TIME_SET;
    Alarm_SetTime.state = ALARM_SET;

    static uint32_t NTime, STime, ATime;
    STime = 0;

//    uint32_t ulSuperState;
//    uint32_t ulState;
//    uint32_t ulSubState;
    State = TIME_SET;
    bool bNEWSTATE = true;
    bool bStartUp = true;

    uint32_t ulSubStateCounter;

    uint32_t T; // tick counter

    /* Prevent the compiler warning about the unused parameter. */
    (void)pvParameters;


    ulReceivedValue = btnNONE;

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
                if(bNEWSTATE)
                {
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
                    ulQueuedValue |= act_fld; 
                    Serial.println(ulQueuedValue);
                    bNEWSTATE = false;
                }
                switch(ulReceivedValue)
                {
                    case btnSELECT:
                        if(!bStartUp)
                        {
                            State = NORMAL;
                            bNEWSTATE = true;
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
                                uint32_t hr = (uint32_t)(STime/3600UL);
                                uint32_t min = (uint32_t)((STime - hr*3600UL)/60UL);
                                min++;
                                if(min>59UL)
                                    min = 0UL;
                                STime = hr*3600UL+min*60UL;
                                break;    
                        }
                        break;
                        
                    case btnDOWN:
                        bStartUp = false;
                        // Clear StartUp Bit
                        ulQueuedValue &= ~(STARTUP_MASK);
                        switch(act_fld)
                        {
                            case ACT_HH:
                                Serial.print("Incrementing down hour "); Serial.println(STime);
                                if(STime<3600UL)
                                    STime += 23UL*3600UL;
                                else
                                    STime -= 3600UL;
                                break;

                            case ACT_MM:
                                uint32_t hr = (uint32_t)(STime/3600UL);
                                uint32_t min = (uint32_t)((STime - hr*3600UL)/60UL);
                                if(min==0UL)
                                    min = 59UL;
                                else
                                    min--;
                                STime = hr*3600UL+min*60UL;
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

                    // Change State
                    ulQueuedValue &= ~(STATE_MASK);
                    ulQueuedValue |= State;
                }
                switch(ulReceivedValue)
                {
                    case btnSELECT:
                        State = NORMAL;
                        bNEWSTATE = true;
                        break;
                    
                }
                break;
                if(bNEWSTATE)
                {
                    act_fld = ACT_HH;
                    ulQueuedValue &= ~(FLD_MASK);
                    ulQueuedValue != act_fld;

                    // Change State
                    ulQueuedValue &= ~(STATE_MASK);
                    ulQueuedValue |= State;
                    
                    // Clear and Replace STime
                    ulQueuedValue &= ~(TIME_MASK);
                    ulQueuedValue |= ATime;
                    
                    Serial.println(ulQueuedValue);
                    bNEWSTATE = false;
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
                        break;
                    case btnRIGHT:
                        if(act_fld == ACT_HH)
                            act_fld = ACT_MM;
                        break;

                    case btnUP:
                        switch(act_fld)
                        {
                            case ACT_HH:
                                Serial.println("Incrementing up hour");
                                ATime += 3600UL;
                                if(ATime >= 24UL*3600UL)
                                    ATime -= 24UL*3600UL;
                                break;

                            case ACT_MM:
                                uint32_t hr = (uint32_t)(ATime/3600UL);
                                uint32_t min = (uint32_t)((ATime - hr*3600UL)/60UL);
                                min++;
                                if(min>59UL)
                                    min = 0UL;
                                ATime = hr*3600UL+min*60UL;
                                break;    
                        }
                        break;
                        
                    case btnDOWN:
                        switch(act_fld)
                        {
                            case ACT_HH:
                                Serial.print("Incrementing down hour "); Serial.println(ATime);
                                if(ATime<3600UL)
                                    ATime += 23UL*3600UL;
                                else
                                    ATime -= 3600UL;
                                break;

                            case ACT_MM:
                                uint32_t hr = (uint32_t)(ATime/3600UL);
                                uint32_t min = (uint32_t)((ATime - hr*3600UL)/60UL);
                                if(min==0UL)
                                    min = 59UL;
                                else
                                    min--;
                                ATime = hr*3600UL+min*60UL;
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
                ulQueuedValue |= ATime;
                break;
        }

        switch(ulReceivedValue)
        {
//            case btnNONE:
//                Serial.println("NONE");
//                break;
//            case btnRIGHT:
//                Serial.println("RIGHT");
//                break;
//            case btnUP:
//                Serial.println("UP");
//                break;
//            case btnDOWN:
//                Serial.println("DOWN");
//                break;
//            case btnLEFT:
//                Serial.println("LEFT");
//                break;
//            case btnSELECT:
//                Serial.println("SELECT");
//                break;
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
    const char am[]="AM";
    const char pm[]="PM";
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
        uint32_t curstate = ulReceivedValue&STATE_MASK;
        Serial.print("State Mask:  "); Serial.println(curstate);
        uint32_t curfield = ulReceivedValue&FLD_MASK;
        Serial.print("Field Mask:  "); Serial.println(curfield);
      
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


        switch(curstate)
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

            break;

            case TIME_SET:

                if(curfield == ACT_HH)
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

                if(curfield == ACT_MM)
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
                if(curfield == ACT_HH)
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
                if(curfield == ACT_HH)
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

                if(curfield == ACT_MM)
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
                if(curfield == ACT_HH)
                {
                    if(bFlash)
                        lcd.print(meridien);
                    else
                        lcd.print("  ");
                }
                else
                    lcd.print(meridien);

                lcd.setCursor(2,1);
                lcd.print("Set Alarm");

            break;
        }

    
    }
}
/*-----------------------------------------------------------*/


