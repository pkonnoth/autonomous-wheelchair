#include <Arduino.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Brake relay signal pins
constexpr uint8_t BRAKE_RELAY_1_PIN = 0;
constexpr uint8_t BRAKE_RELAY_2_PIN = 2;

// Right motor driver pins (BTS7960-style naming)
constexpr uint8_t RIGHT_R_EN_PIN = 32;
constexpr uint8_t RIGHT_L_EN_PIN = 33;
constexpr uint8_t RIGHT_R_PWM_PIN = 25;
constexpr uint8_t RIGHT_L_PWM_PIN = 26;

// Left motor driver pins (BTS7960-style naming)
constexpr uint8_t LEFT_R_EN_PIN = 4;
constexpr uint8_t LEFT_L_EN_PIN = 16;
constexpr uint8_t LEFT_R_PWM_PIN = 17;
constexpr uint8_t LEFT_L_PWM_PIN = 5;

// PWM setup
constexpr uint8_t RIGHT_R_PWM_CH = 0;
constexpr uint8_t RIGHT_L_PWM_CH = 1;
constexpr uint8_t LEFT_R_PWM_CH = 2;
constexpr uint8_t LEFT_L_PWM_CH = 3;
constexpr uint32_t PWM_FREQ = 20000;
constexpr uint8_t PWM_RES_BITS = 8;

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint16_t BRAKE_RELEASE_DELAY_MS = 250;
constexpr uint16_t BRAKE_ENGAGE_DELAY_MS = 120;
constexpr uint16_t COMMAND_TIMEOUT_MS = 250;
constexpr uint8_t MAX_SPEED = 255;
constexpr bool INVERT_LEFT_MOTOR = true;

bool brakeReleased = false;
int leftSpeedCmd = 0;
int rightSpeedCmd = 0;
uint32_t lastCommandMs = 0;

char commandBuf[48];
size_t commandLen = 0;

void setBrake(bool release) {
  if (brakeReleased == release) {
    return;
  }

  digitalWrite(BRAKE_RELAY_1_PIN, release ? HIGH : LOW);
  digitalWrite(BRAKE_RELAY_2_PIN, release ? HIGH : LOW);
  brakeReleased = release;

  if (release) {
    delay(BRAKE_RELEASE_DELAY_MS);
  }
}

void driveBts7960Signed(uint8_t rEnPin, uint8_t lEnPin, uint8_t rPwmCh, uint8_t lPwmCh,
                       int speed) {
  speed = constrain(speed, -static_cast<int>(MAX_SPEED), static_cast<int>(MAX_SPEED));

  if (speed == 0) {
    ledcWrite(rPwmCh, 0);
    ledcWrite(lPwmCh, 0);
    return;
  }

  digitalWrite(rEnPin, HIGH);
  digitalWrite(lEnPin, HIGH);

  if (speed > 0) {
    ledcWrite(lPwmCh, 0);
    ledcWrite(rPwmCh, static_cast<uint8_t>(speed));
  } else {
    ledcWrite(rPwmCh, 0);
    ledcWrite(lPwmCh, static_cast<uint8_t>(-speed));
  }
}

void setMotorSpeeds(int leftSpeed, int rightSpeed) {
  leftSpeed = constrain(leftSpeed, -static_cast<int>(MAX_SPEED), static_cast<int>(MAX_SPEED));
  rightSpeed = constrain(rightSpeed, -static_cast<int>(MAX_SPEED), static_cast<int>(MAX_SPEED));

  if (INVERT_LEFT_MOTOR) {
    leftSpeed = -leftSpeed;
  }

  leftSpeedCmd = leftSpeed;
  rightSpeedCmd = rightSpeed;

  driveBts7960Signed(LEFT_R_EN_PIN, LEFT_L_EN_PIN, LEFT_R_PWM_CH, LEFT_L_PWM_CH, leftSpeed);
  driveBts7960Signed(RIGHT_R_EN_PIN, RIGHT_L_EN_PIN, RIGHT_R_PWM_CH, RIGHT_L_PWM_CH, rightSpeed);
}

void stopThenEngageBrake() {
  const bool wasRunning = (leftSpeedCmd != 0 || rightSpeedCmd != 0);
  setMotorSpeeds(0, 0);

  if (brakeReleased) {
    if (wasRunning) {
      delay(BRAKE_ENGAGE_DELAY_MS);
    }
    setBrake(false);
  }
}

void processCommand(char* line) {
  if (!line || line[0] == '\0') {
    return;
  }

  const char cmd = toupper(line[0]);
  char* arg = strchr(line, ',');
  int value = 0;

  if (arg) {
    value = atoi(arg + 1);
  }

  switch (cmd) {
    case 'F':
      if (value <= 0) {
        value = 160;
      }
      setBrake(true);
      setMotorSpeeds(value, value);
      break;

    case 'R':
      if (value <= 0) {
        value = 160;
      }
      setBrake(true);
      setMotorSpeeds(-value, -value);
      break;

    case 'M':
      value = constrain(value, -static_cast<int>(MAX_SPEED), static_cast<int>(MAX_SPEED));
      if (value == 0) {
        stopThenEngageBrake();
      } else {
        setBrake(true);
        setMotorSpeeds(value, value);
      }
      break;

    case 'A':
      if (value <= 0) {
        value = 160;
      }
      setBrake(true);
      setMotorSpeeds(-value, value);
      break;

    case 'D':
      if (value <= 0) {
        value = 160;
      }
      setBrake(true);
      setMotorSpeeds(value, -value);
      break;

    case 'X': {
      int left = 0;
      int right = 0;
      char* pair = strchr(line, ',');
      if (!pair || sscanf(pair + 1, "%d,%d", &left, &right) != 2) {
        stopThenEngageBrake();
      } else {
        left = constrain(left, -static_cast<int>(MAX_SPEED), static_cast<int>(MAX_SPEED));
        right = constrain(right, -static_cast<int>(MAX_SPEED), static_cast<int>(MAX_SPEED));
        if (left == 0 && right == 0) {
          stopThenEngageBrake();
        } else {
          setBrake(true);
          setMotorSpeeds(left, right);
        }
      }
      break;
    }

    case 'B':
      if (value != 0) {
        setBrake(true);
      } else {
        stopThenEngageBrake();
      }
      break;

    case 'S':
    default:
      stopThenEngageBrake();
      break;
  }

  lastCommandMs = millis();
}

void readSerialCommands() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());

    if (c == '\n') {
      commandBuf[commandLen] = '\0';
      processCommand(commandBuf);
      commandLen = 0;
      continue;
    }

    if (c == '\r') {
      continue;
    }

    if (commandLen < sizeof(commandBuf) - 1) {
      commandBuf[commandLen++] = c;
    } else {
      commandLen = 0;
    }
  }
}

void setup() {
  pinMode(BRAKE_RELAY_1_PIN, OUTPUT);
  pinMode(BRAKE_RELAY_2_PIN, OUTPUT);
  pinMode(RIGHT_R_EN_PIN, OUTPUT);
  pinMode(RIGHT_L_EN_PIN, OUTPUT);
  pinMode(LEFT_R_EN_PIN, OUTPUT);
  pinMode(LEFT_L_EN_PIN, OUTPUT);

  digitalWrite(RIGHT_R_EN_PIN, LOW);
  digitalWrite(RIGHT_L_EN_PIN, LOW);
  digitalWrite(LEFT_R_EN_PIN, LOW);
  digitalWrite(LEFT_L_EN_PIN, LOW);

  ledcSetup(RIGHT_R_PWM_CH, PWM_FREQ, PWM_RES_BITS);
  ledcSetup(RIGHT_L_PWM_CH, PWM_FREQ, PWM_RES_BITS);
  ledcSetup(LEFT_R_PWM_CH, PWM_FREQ, PWM_RES_BITS);
  ledcSetup(LEFT_L_PWM_CH, PWM_FREQ, PWM_RES_BITS);

  ledcAttachPin(RIGHT_R_PWM_PIN, RIGHT_R_PWM_CH);
  ledcAttachPin(RIGHT_L_PWM_PIN, RIGHT_L_PWM_CH);
  ledcAttachPin(LEFT_R_PWM_PIN, LEFT_R_PWM_CH);
  ledcAttachPin(LEFT_L_PWM_PIN, LEFT_L_PWM_CH);

  stopThenEngageBrake();

  Serial.begin(SERIAL_BAUD);
  lastCommandMs = millis();
}

void loop() {
  readSerialCommands();

  if (millis() - lastCommandMs > COMMAND_TIMEOUT_MS) {
    stopThenEngageBrake();
    lastCommandMs = millis();
  }
}
