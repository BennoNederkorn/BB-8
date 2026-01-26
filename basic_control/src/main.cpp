#include <Arduino.h>
// #include <Stepper.h>
#include <AccelStepper.h> // non-stopping Stepper Library
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

#include <driver/mcpwm.h>
#include <driver/pcnt.h>

// ==========================================
// 1. PIN DEFINITIONS
// ==========================================

// Stepper (Head)
#define STEPPER_PIN1 19
#define STEPPER_PIN3 5
#define STEPPER_PIN2 18
#define STEPPER_PIN4 17

// I2C IMU
#define SDA_PIN 21
#define SCL_PIN 22
#define MPU_ADDR 0x68

// Motor A (Left) - Connected to H-Bridge L
#define MOT_A_L_EN 13
// #define MOT_A_R_EN 12 // Failed to communicate with the flash chip is GPIO 12 is used
#define MOT_A_R_EN 4   //
#define MOT_A_L_PWM 27 // Diagram "PWM1"
#define MOT_A_R_PWM 14 // Diagram "PWM2"

// Motor B (Right) - Connected to H-Bridge L
#define MOT_B_L_PWM 33 // Diagram "PWM3"
#define MOT_B_R_PWM 32 // Diagram "PWM4"
#define MOT_B_L_EN 26
#define MOT_B_R_EN 25

// Encoders (Assigned to Input-Only Pins for safety)
#define MOT_A_VOUTA 34
#define MOT_A_VOUTB 35
#define MOT_B_VOUTA 36
#define MOT_B_VOUTB 39

// PCNT Units for Encoders
#define PCNT_UNIT_A PCNT_UNIT_0
#define PCNT_UNIT_B PCNT_UNIT_1

// onboard Led
#define ONBOARD_LED 1

// ==========================================
// 2. GLOBAL VARIABLES (Shared between cores)
// ==========================================

// 'volatile' is used for variables shared between ISR/Tasks
Adafruit_MPU6050 mpu;
volatile float currentPitch = 0.0;
volatile float currentRoll = 0.0;
volatile float currentYaw = 0.0;

volatile int16_t encCountA = 0;
volatile int16_t encCountB = 0;

AccelStepper headStepper(AccelStepper::FULL4WIRE, STEPPER_PIN1, STEPPER_PIN3, STEPPER_PIN2, STEPPER_PIN4);
const int MAX_STEPPER_SPPED = 600; // Steps per second

// Shared data struct to hold received serial data
struct CommandData
{
    bool ai_mode = false;
    float head_direction = 0.0;
    float head_force = 0.0;
    float body_direction = 0.0;
    float body_force = 0.0;
};
CommandData sharedCmd;
SemaphoreHandle_t dataMutex;

// local data to hold the snapshot
bool ai_mode = false;
float head_direction = 0.0;
float head_force = 0.0;
float body_direction = 0.0;
float body_force = 0.0;

// ==========================================
// 3. HELPER FUNCTIONS
// ==========================================

void setupMotors()
{
    // Initialize Enable Pins
    pinMode(MOT_A_L_EN, OUTPUT);
    pinMode(MOT_A_R_EN, OUTPUT);
    pinMode(MOT_B_L_EN, OUTPUT);
    pinMode(MOT_B_R_EN, OUTPUT);

    // Enable drivers (Set High to enable H-Bridge)
    digitalWrite(MOT_A_L_EN, HIGH);
    digitalWrite(MOT_A_R_EN, HIGH);
    digitalWrite(MOT_B_L_EN, HIGH);
    digitalWrite(MOT_B_R_EN, HIGH);

    // Initialize MCPWM for Motor A
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, MOT_A_L_PWM);
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0B, MOT_A_R_PWM);

    // Initialize MCPWM for Motor B
    mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM0A, MOT_B_L_PWM);
    mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM0B, MOT_B_R_PWM);

    mcpwm_config_t pwm_config;
    pwm_config.frequency = 20000; // 20kHz frequency (inaudible)
    pwm_config.cmpr_a = 0;
    pwm_config.cmpr_b = 0;
    pwm_config.counter_mode = MCPWM_UP_COUNTER;
    pwm_config.duty_mode = MCPWM_DUTY_MODE_0;

    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);
    mcpwm_init(MCPWM_UNIT_1, MCPWM_TIMER_0, &pwm_config);
}

void setupHeadStepper()
{
    // headStepper.setSpeed(10);      // We will set speed in the loop
    headStepper.setMaxSpeed(MAX_STEPPER_SPPED); // Steps per second
    headStepper.setAcceleration(500);
}

void setMotorSpeed(mcpwm_unit_t unit, float speed)
{
    // speed is -100.0 to 100.0
    if (speed > 0)
    {
        mcpwm_set_duty(unit, MCPWM_TIMER_0, MCPWM_OPR_A, speed);
        mcpwm_set_duty(unit, MCPWM_TIMER_0, MCPWM_OPR_B, 0); // Low
        mcpwm_set_duty_type(unit, MCPWM_TIMER_0, MCPWM_OPR_A, MCPWM_DUTY_MODE_0);
    }
    else
    {
        mcpwm_set_duty(unit, MCPWM_TIMER_0, MCPWM_OPR_A, 0); // Low
        mcpwm_set_duty(unit, MCPWM_TIMER_0, MCPWM_OPR_B, -speed);
        mcpwm_set_duty_type(unit, MCPWM_TIMER_0, MCPWM_OPR_B, MCPWM_DUTY_MODE_0);
    }
}

// --- ENCODER SETUP (PCNT Hardware) ---
void setupEncoder(int unit, int pinA, int pinB)
{
    // TODO
}

// --- IMU HELPER (Basic Reading) ---
void readIMU()
{
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    currentYaw = a.acceleration.x;
    currentPitch = a.acceleration.y;
    currentRoll = a.acceleration.z;
    // Serial.printf("currentYaw: %.2f, currentPitch: %.2f, currentRoll: %.2f\n", currentYaw, currentPitch, currentRoll);

    // TODO handle I2C errors here to prevent locking up
}

// // returns a value between 1 and -1 for values between 0 and 90
// float smooth(float angle)
// {
//   return math.cos((1 / 90) * angle / (2 * math.pi));
// }

// --- CORE 1: REAL-TIME CONTROL TASK ---
void controlTask(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(10); // 10ms = 100Hz Loop

    float output_A = 0.0;
    float output_B = 0.0;

    for (;;)
    {
        // Serial.println("CORE 1 Loop: realtime");

        // Take the mutex and copy the data to local vars to give it back quickly.
        if (xSemaphoreTake(dataMutex, (TickType_t)5) == pdTRUE)
        {
            ai_mode = sharedCmd.ai_mode;
            body_direction = sharedCmd.body_direction;
            body_force = sharedCmd.body_force;
            xSemaphoreGive(dataMutex);
        }

        readIMU();

        // TODO Encoders:
        // pcnt_get_counter_value(PCNT_UNIT_A, (int16_t *)&encCountA);
        // pcnt_get_counter_value(PCNT_UNIT_B, (int16_t *)&encCountB);

        // TODO: more advanced logic

        float speed = body_force * 100.0;
        if (body_direction < 90.0)
        {
            output_A = speed;
            output_B = speed * (1.0 - 2.0 * (body_direction / 90.0));
        }
        else if (body_direction < 180.0)
        {
            output_A = speed * (1.0 - 2.0 * ((body_direction - 90.0) / 90.0));
            output_B = -speed;
        }
        else if (body_direction < 270.0)
        {
            output_A = -speed;
            output_B = speed * (-1.0 + 2.0 * ((body_direction - 180.0) / 90.0));
        }
        else
        {
            output_A = speed * (-1.0 + 2.0 * ((body_direction - 270.0) / 90.0));
            output_B = speed;
        }

        // 3. WRITE MOTORS
        setMotorSpeed(MCPWM_UNIT_0, output_A);
        setMotorSpeed(MCPWM_UNIT_1, output_B);

        // Wait specifically until 10ms have passed since last run
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// --- CORE 0/1: SETUP & SERIAL ---
void setup()
{
    Serial.begin(115200);
    // Serial.println("BB-8 Controller Starting...");
    dataMutex = xSemaphoreCreateMutex();

    // pinMode(ONBOARD_LED, OUTPUT);
    Wire.begin(SDA_PIN, SCL_PIN, 400000); // Fast I2C

    if (!mpu.begin(MPU_ADDR, &Wire))
    {
        // Serial.println("Failed to find MPU6050 chip!");
        // Failed to find MPU6050 chip
        while (1)
        {
            delay(10); // Halt here if sensor fails
        }
    }

    // Setup MPU ranges if necessary
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

    setupMotors();
    setupHeadStepper();
    // TODO Encoders:
    // setupEncoder(PCNT_UNIT_A, MOT_A_VOUTA, MOT_A_VOUTB);
    // setupEncoder(PCNT_UNIT_B, MOT_B_VOUTA, MOT_B_VOUTB);

    // Launch Control Task pinned to Core 1
    xTaskCreatePinnedToCore(
        controlTask,   // Function
        "ControlLoop", // Name
        4096,          // Stack size
        NULL,          // Parameters
        1,             // Priority (1 = High)
        NULL,          // Task Handle
        1              // Core ID (1 = App Core)
    );
}

// --- CORE 0: Serial and non-realtime tasks ---
void loop()
{
    // Serial.println("CORE 0 Loop: serial, stepper");

    // This loop handles Serial communication and the stepper motor "in the background"
    // It will not slow down the motors because they are in the 'controlTask'

    // Check for incoming serial commands here
    if (Serial.available() > 0)
    {
        // Read the line until a newline character
        String line = Serial.readStringUntil('\n');

        // Simple Parsing using sscanf
        // temporary variables to ensure data integrity during parsing
        int temp_ai;
        float temp_hd, temp_hf, temp_bd, temp_bf;

        // sscanf parses the CSV string. Returns number of items successfully matched.
        int items = sscanf(line.c_str(), "%d,%f,%f,%f,%f",
                           &temp_ai, &temp_hd, &temp_hf, &temp_bd, &temp_bf);

        // If we found all 5 items, update our global variables
        if (items == 5)
        {
            if (xSemaphoreTake(dataMutex, (TickType_t)10) == pdTRUE)
            {
                sharedCmd.ai_mode = (temp_ai == 1);
                sharedCmd.head_direction = temp_hd;
                sharedCmd.head_force = temp_hf;
                sharedCmd.body_direction = temp_bd;
                sharedCmd.body_force = temp_bf;
                head_direction = temp_hd;
                head_force = temp_hf;
                xSemaphoreGive(dataMutex);
                // new_command_received = true;
            }
        }
    }

    if (head_force != 0.0)
    {
        if (head_direction == 0.0)
        {
            headStepper.setSpeed(MAX_STEPPER_SPPED * head_force); // Set a constant sweep speed (steps/sec)
            headStepper.runSpeed();
            // Serial.printf("CurrentStepperPosition: %d\n", abs(headStepper.currentPosition()));
        }
        if (head_direction == 180.0)
        {
            headStepper.setSpeed(-MAX_STEPPER_SPPED * head_force); // Set a constant sweep speed (steps/sec)
            headStepper.runSpeed();
            // Serial.printf("CurrentStepperPosition: %d\n", abs(headStepper.currentPosition()));
        }
    }
}