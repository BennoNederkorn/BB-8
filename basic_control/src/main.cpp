#include <Arduino.h>
// #include <Stepper.h>
#include <AccelStepper.h> // non-stopping Stepper Library
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <math.h>

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
volatile float currentGyroX = 0.0;
volatile float currentGyroY = 0.0;
volatile float currentGyroZ = 0.0;

volatile int16_t encCountA = 0;
volatile int16_t encCountB = 0;

AccelStepper headStepper(AccelStepper::FULL4WIRE, STEPPER_PIN1, STEPPER_PIN3, STEPPER_PIN2, STEPPER_PIN4);
const int MAX_STEPPER_SPPED = 600; // Steps per second
const int STEPPER_ACCELERATION = 500;
const int TOTAL_STEPPER_STEPS = 4096; // TODO
int head_steps = 0;
volatile int relative_head_direction = 0;

const float MAX_INCLINATION = 20.0; // degree
static float accumulated_error = 0.0;

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

// PID tuning parameters
float Kp = 5.0;
float Kd = 0.0;
float Ki = 0.0;

// ==========================================
// 3. HELPER FUNCTIONS
// ==========================================

// Fast sin/cos lookup tables (1 degree precision)
static const float sinTable[360] = {
    0.000000, 0.017452, 0.034899, 0.052336, 0.069756, 0.087156, 0.104528, 0.121869,
    0.139173, 0.156434, 0.173648, 0.190809, 0.207912, 0.224951, 0.241922, 0.258819,
    0.275637, 0.292372, 0.309017, 0.325568, 0.342020, 0.358368, 0.374607, 0.390731,
    0.406737, 0.422618, 0.438371, 0.453990, 0.469472, 0.484810, 0.500000, 0.515038,
    0.529919, 0.544639, 0.559193, 0.573576, 0.587785, 0.601815, 0.615661, 0.629320,
    0.642788, 0.656059, 0.669131, 0.681998, 0.694658, 0.707107, 0.719340, 0.731354,
    0.743145, 0.754710, 0.766044, 0.777146, 0.788011, 0.798636, 0.809017, 0.819152,
    0.829038, 0.838671, 0.848048, 0.857167, 0.866025, 0.874620, 0.882948, 0.891007,
    0.898794, 0.906308, 0.913545, 0.920505, 0.927184, 0.933580, 0.939693, 0.945519,
    0.951057, 0.956305, 0.961262, 0.965926, 0.970296, 0.974370, 0.978148, 0.981627,
    0.984808, 0.987688, 0.990268, 0.992546, 0.994522, 0.996195, 0.997564, 0.998630,
    0.999391, 0.999848, 1.000000, 0.999848, 0.999391, 0.998630, 0.997564, 0.996195,
    0.994522, 0.992546, 0.990268, 0.987688, 0.984808, 0.981627, 0.978148, 0.974370,
    0.970296, 0.965926, 0.961262, 0.956305, 0.951057, 0.945519, 0.939693, 0.933580,
    0.927184, 0.920505, 0.913545, 0.906308, 0.898794, 0.891007, 0.882948, 0.874620,
    0.866025, 0.857167, 0.848048, 0.838671, 0.829038, 0.819152, 0.809017, 0.798636,
    0.788011, 0.777146, 0.766044, 0.754710, 0.743145, 0.731354, 0.719340, 0.707107,
    0.694658, 0.681998, 0.669131, 0.656059, 0.642788, 0.629320, 0.615661, 0.601815,
    0.587785, 0.573576, 0.559193, 0.544639, 0.529919, 0.515038, 0.500000, 0.484810,
    0.469472, 0.453990, 0.438371, 0.422618, 0.406737, 0.390731, 0.374607, 0.358368,
    0.342020, 0.325568, 0.309017, 0.292372, 0.275637, 0.258819, 0.241922, 0.224951,
    0.207912, 0.190809, 0.173648, 0.156434, 0.139173, 0.121869, 0.104528, 0.087156,
    0.069756, 0.052336, 0.034899, 0.017452, 0.000000, -0.017452, -0.034899, -0.052336,
    -0.069756, -0.087156, -0.104528, -0.121869, -0.139173, -0.156434, -0.173648, -0.190809,
    -0.207912, -0.224951, -0.241922, -0.258819, -0.275637, -0.292372, -0.309017, -0.325568,
    -0.342020, -0.358368, -0.374607, -0.390731, -0.406737, -0.422618, -0.438371, -0.453990,
    -0.469472, -0.484810, -0.500000, -0.515038, -0.529919, -0.544639, -0.559193, -0.573576,
    -0.587785, -0.601815, -0.615661, -0.629320, -0.642788, -0.656059, -0.669131, -0.681998,
    -0.694658, -0.707107, -0.719340, -0.731354, -0.743145, -0.754710, -0.766044, -0.777146,
    -0.788011, -0.798636, -0.809017, -0.819152, -0.829038, -0.838671, -0.848048, -0.857167,
    -0.866025, -0.874620, -0.882948, -0.891007, -0.898794, -0.906308, -0.913545, -0.920505,
    -0.927184, -0.933580, -0.939693, -0.945519, -0.951057, -0.956305, -0.961262, -0.965926,
    -0.970296, -0.974370, -0.978148, -0.981627, -0.984808, -0.987688, -0.990268, -0.992546,
    -0.994522, -0.996195, -0.997564, -0.998630, -0.999391, -0.999848, -1.000000, -0.999848,
    -0.999391, -0.998630, -0.997564, -0.996195, -0.994522, -0.992546, -0.990268, -0.987688,
    -0.984808, -0.981627, -0.978148, -0.974370, -0.970296, -0.965926, -0.961262, -0.956305,
    -0.951057, -0.945519, -0.939693, -0.933580, -0.927184, -0.920505, -0.913545, -0.906308,
    -0.898794, -0.891007, -0.882948, -0.874620, -0.866025, -0.857167, -0.848048, -0.838671,
    -0.829038, -0.819152, -0.809017, -0.798636, -0.788011, -0.777146, -0.766044, -0.754710,
    -0.743145, -0.731354, -0.719340, -0.707107, -0.694658, -0.681998, -0.669131, -0.656059,
    -0.642788, -0.629320, -0.615661, -0.601815, -0.587785, -0.573576, -0.559193, -0.544639,
    -0.529919, -0.515038, -0.500000, -0.484810, -0.469472, -0.453990, -0.438371, -0.422618,
    -0.406737, -0.390731, -0.374607, -0.358368, -0.342020, -0.325568, -0.309017, -0.292372,
    -0.275637, -0.258819, -0.241922, -0.224951, -0.207912, -0.190809, -0.173648, -0.156434,
    -0.139173, -0.121869, -0.104528, -0.087156, -0.069756, -0.052336, -0.034899, -0.017452};

inline float fastSin(float degrees)
{
    int idx = ((int)degrees) % 360;
    if (idx < 0)
        idx += 360;
    return sinTable[idx];
}

inline float fastCos(float degrees)
{
    return fastSin(degrees + 90.0);
}

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
    headStepper.setAcceleration(STEPPER_ACCELERATION);
}

void setMotorSpeed(mcpwm_unit_t unit, float speed)
{
    // speed is -100.0 to 100.0
    if (speed > 0)
    {
        mcpwm_set_duty(unit, MCPWM_TIMER_0, MCPWM_OPR_A, speed);
        mcpwm_set_duty(unit, MCPWM_TIMER_0, MCPWM_OPR_B, 0); // Low
        // mcpwm_set_duty_type(unit, MCPWM_TIMER_0, MCPWM_OPR_A, MCPWM_DUTY_MODE_0);
    }
    else
    {
        mcpwm_set_duty(unit, MCPWM_TIMER_0, MCPWM_OPR_A, 0); // Low
        mcpwm_set_duty(unit, MCPWM_TIMER_0, MCPWM_OPR_B, -speed);
        // mcpwm_set_duty_type(unit, MCPWM_TIMER_0, MCPWM_OPR_B, MCPWM_DUTY_MODE_0);
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
    // TODO check if these values do make sense!
    currentGyroX = g.gyro.x;             // m/s
    currentGyroY = g.gyro.y;             // m/s
    currentGyroZ = g.gyro.z;             // m/s
    currentYaw = a.acceleration.x + 2.4; // hardcode so that we don't need calibration.
    currentPitch = a.acceleration.y;
    currentRoll = a.acceleration.z;
    // Serial.printf("currentYaw: %.2f, currentPitch: %.2f, currentRoll: %.2f, currentGyroX: %.2f, currentGyroY: %.2f, currentGyroZ: %.2f\n", currentYaw, currentPitch, currentRoll, currentGyroX, currentGyroY, currentGyroZ);

    // TODO handle I2C errors here to prevent locking up
}

// --- CORE 1: REAL-TIME CONTROL TASK ---
void controlTask(void *pvParameters)
{
    // TODO check if this is fast enough
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

        // Testcode:e
        // ai_mode = true;
        // body_force = 1.0;
        // body_direction = body_direction + 0.1;
        // if (body_direction > 360.0)
        //     body_direction = 0.0;

        readIMU();

        // TODO: where is the center of gravity?

        // ASSUMPTION: BB8 stabilizes itself
        // CONTROL APPROACH:
        // - decoupling of the Drive (Velocity) from the Steering (Heading),
        // - even though both are controlled by the same two motors.

        // TODO Encoders needed?
        // pcnt_get_counter_value(PCNT_UNIT_A, (int16_t *)&encCountA);
        // pcnt_get_counter_value(PCNT_UNIT_B, (int16_t *)&encCountB);

        if (ai_mode)
        {

            // 0 deg is turn right
            // 90 deg is drive forward
            // 180 deg is turn left
            // 270 deg is drive backward
            float forward_request = body_force * fastSin(body_direction);
            float turn_request = body_force * fastCos(body_direction);

            // drive forward, tilt up (Yaw < 0)
            // drive backward, tilt up (Yaw > 0)
            float inclination_goal = forward_request * MAX_INCLINATION;

            float inclination_error = currentYaw - inclination_goal;
            accumulated_error += inclination_error;
            // TODO cap this accumulation? How fast does this windup happen?
            accumulated_error = constrain(accumulated_error, -50, 50);

            float P_term = Kp * inclination_error;
            float D_term = Kd * (-currentGyroX); // TODO minus
            float I_term = Ki * accumulated_error;

            float inclination_output = P_term + I_term + D_term;
            float turn_output = turn_request * 100; // from -100 (turn left) to 100 (turn right)

            // // FORWARDS
            // output_A = -100; // left
            // output_B = 100;  // right
            // // TURN LEFT
            // output_A = 100; // left
            // output_B = 100; // right
            // // BACKWARDS
            // output_A = 100;  // left
            // output_B = -100; // right
            // // TURN RIGHT
            // output_A = -100; // left
            // output_B = -100; // right
            float output_A = turn_request - (inclination_output);
            float output_B = turn_request + (inclination_output);

            output_A = constrain(output_A, -100.0, 100.0);
            output_B = constrain(output_B, -100.0, 100.0);

            // Serial.printf("body_direction: %.2f, forward_request: %.2f, turn_request: %.2f, inclination_goal: %.2f, inclination_error: %.2f, currentYaw: %.2f, currentGyroX: %.2f, inclination_output: %.2f \n", body_direction, forward_request, turn_request, inclination_goal, inclination_error, currentYaw, currentGyroX, inclination_output);
        }
        else
        {
            float speed = body_force * 100.0;
            if (0.0 <= body_direction && body_direction < 90.0) // turn right (0) --> forward (90)
            {
                output_A = -speed;                                      // left fullspeed
                output_B = speed * fastCos(2 * body_direction + 180.0); // right becomes faster
            }
            else if (90.0 <= body_direction && body_direction < 180.0) // forward (90) --> turn left (180)
            {
                output_A = speed * fastCos(2 * (body_direction - 90.0) + 180.0); // left becomes slower
                output_B = speed;                                                // right fullspeed
            }
            else if (180.0 <= body_direction && body_direction < 270.0) // turn left (180) --> backwards (270)
            {
                output_A = speed;                                         // left fullspeed backwards
                output_B = speed * fastCos(2 * (body_direction - 180.0)); // right becomes slower
            }
            else // (270.0 <= body_direction && body_direction < 360.0) // backwards (270) --> turn right (0)
            {
                output_A = speed * fastCos(2 * (body_direction - 270.0)); // left becomes faster
                output_B = -speed;                                        // right fullspeed backwards
            }
        }

        // WRITE MOTORS
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
        float temp_kp, temp_ki, temp_kd;

        // sscanf parses the CSV string. Returns number of items successfully matched.
        int items = sscanf(line.c_str(), "%d,%f,%f,%f,%f,%f,%f,%f",
                           &temp_ai, &temp_hd, &temp_hf, &temp_bd, &temp_bf, &temp_kp, &temp_ki, &temp_kd);

        // If we found all 5 items, update our global variables
        if (items == 8)
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
                Kp = temp_kp;
                Ki = temp_ki;
                Kd = temp_kd;
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
        // head_steps = abs(headStepper.currentPosition());
        // relative_head_direction = head_steps * (360 / TOTAL_STEPPER_STEPS);
    }
}