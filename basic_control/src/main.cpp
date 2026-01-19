#include <Arduino.h>
#include <Stepper.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

// ==========================================
// 1. PIN DEFINITIONS
// ==========================================

// --- Stepper (Head) ---
const int stepsPerRevolution = 2048; 
Stepper myStepper(stepsPerRevolution, 19, 5, 18, 17); // IN1, IN3, IN2, IN4

// --- DC Motors ---
#define MOT_A_RPWM 14
#define MOT_A_LPWM 27
#define MOT_A_R_EN 12
#define MOT_A_L_EN 13

#define MOT_B_RPWM 32
#define MOT_B_LPWM 33
#define MOT_B_R_EN 25
#define MOT_B_L_EN 26

// PWM Channels
const int freq = 5000;
const int resolution = 8;
#define CH_A_RPWM 0
#define CH_A_LPWM 1
#define CH_B_RPWM 2
#define CH_B_LPWM 3

// ==========================================
// 2. GLOBAL VARIABLES (Shared between cores)
// ==========================================
Adafruit_MPU6050 mpu;

// Volatile means: "This variable changes unexpectedly, don't cache it."
volatile float currentTilt = 0.0;
volatile float motorPower = 0.0; 

// Task Handle
TaskHandle_t TaskBalance; 

// ==========================================
// 3. HELPER FUNCTIONS
// ==========================================
void setMotorSpeed(int speed) {
  // speed: -255 to 255 (Applies to BOTH motors for balancing)
  // Clamp values to prevent errors
  if(speed > 255) speed = 255;
  if(speed < -255) speed = -255;

  if (speed > 0) {
    // Forward
    ledcWrite(CH_A_RPWM, speed); ledcWrite(CH_A_LPWM, 0);
    ledcWrite(CH_B_RPWM, speed); ledcWrite(CH_B_LPWM, 0);
  } else if (speed < 0) {
    // Backward
    ledcWrite(CH_A_RPWM, 0); ledcWrite(CH_A_LPWM, abs(speed));
    ledcWrite(CH_B_RPWM, 0); ledcWrite(CH_B_LPWM, abs(speed));
  } else {
    // Stop
    ledcWrite(CH_A_RPWM, 0); ledcWrite(CH_A_LPWM, 0);
    ledcWrite(CH_B_RPWM, 0); ledcWrite(CH_B_LPWM, 0);
  }
}

// ==========================================
// 4. THE CRITICAL TASK (Core 0)
// This runs independent of the main loop!
// ==========================================
void BalanceCode( void * pvParameters ){
  Serial.print("Balance Task running on Core: ");
  Serial.println(xPortGetCoreID());

  // Define loop timing (e.g., 100Hz = every 10ms)
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(10); // 10ms delay
  xLastWakeTime = xTaskGetTickCount();

  for(;;){ // Infinite Loop
    
    // --- A. READ IMU ---
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    // Simplified Tilt Calculation (Ideally use a Complementary Filter later)
    // For BB-8 "Hamster" drive, we usually care about Pitch or Roll.
    // Let's assume 'Y' axis is our tilt axis for now.
    currentTilt = a.acceleration.y; 

    // --- B. CALCULATE PID (Placeholder) ---
    // Here is where the math happens later.
    // For now, let's just do a "Dummy Test": 
    // If we tilt > 2m/s^2, turn motors on slowly.
    
    if (currentTilt > 2.0) {
      motorPower = 60;  // Lean Forward -> Drive Forward
    } else if (currentTilt < -2.0) {
      motorPower = -60; // Lean Back -> Drive Back
    } else {
      motorPower = 0;   // Safe Zone
    }

    // --- C. WRITE TO MOTORS ---
    setMotorSpeed((int)motorPower);

    // --- D. WAIT ---
    // This command ensures the loop runs exactly every 10ms,
    // regardless of how long the math took.
    vTaskDelayUntil( &xLastWakeTime, xFrequency );
  } 
}

// ==========================================
// 5. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  
  // --- Init IMU ---
  if (!mpu.begin()) {
    Serial.println("IMU Failed!");
    while (1);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  // --- Init DC Motors ---
  pinMode(MOT_A_R_EN, OUTPUT); pinMode(MOT_A_L_EN, OUTPUT);
  pinMode(MOT_B_R_EN, OUTPUT); pinMode(MOT_B_L_EN, OUTPUT);
  digitalWrite(MOT_A_R_EN, HIGH); digitalWrite(MOT_A_L_EN, HIGH);
  digitalWrite(MOT_B_R_EN, HIGH); digitalWrite(MOT_B_L_EN, HIGH);

  ledcSetup(CH_A_RPWM, freq, resolution);
  ledcSetup(CH_A_LPWM, freq, resolution);
  ledcSetup(CH_B_RPWM, freq, resolution);
  ledcSetup(CH_B_LPWM, freq, resolution);
  ledcAttachPin(MOT_A_RPWM, CH_A_RPWM); ledcAttachPin(MOT_A_LPWM, CH_A_LPWM);
  ledcAttachPin(MOT_B_RPWM, CH_B_RPWM); ledcAttachPin(MOT_B_LPWM, CH_B_LPWM);

  // --- Init Stepper ---
  myStepper.setSpeed(10); // Slow speed

  // --- LAUNCH PARALLEL TASK ---
  // xTaskCreatePinnedToCore( Function, Name, StackSize, Params, Priority, Handle, CoreID )
  xTaskCreatePinnedToCore(
    BalanceCode,   // Function to call
    "BalanceTask", // Name for debugging
    10000,         // Stack size (bytes)
    NULL,          // Parameters
    1,             // Priority (1 = High)
    &TaskBalance,  // Task Handle
    0              // Run on Core 0 (Main loop runs on Core 1)
  );
}

// ==========================================
// 6. MAIN LOOP (Core 1)
// Handles "Slow" stuff like Head Movement
// ==========================================
void loop() {
  
  // Example: Move head slowly back and forth
  // Even though 'myStepper.step' BLOCKS execution for a split second,
  // Core 0 (BalanceCode) will keep running perfectly!
  
  Serial.print("Main Loop (Core 1) - Tilt: ");
  Serial.println(currentTilt); // Read the value updated by Core 0

  myStepper.step(100); // Move head
  delay(500);
  myStepper.step(-100); // Move head back
  delay(500);
}