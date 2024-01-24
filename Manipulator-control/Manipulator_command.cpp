#include "esp32-hal-gpio.h"
#include "rom/spi_flash.h"
#include "Manipulator_Config.h"
#include "Wire.h"
#include "esp32-hal.h"
#include "Manipulator_command.h"

Manipulator_command::Manipulator_command(ESP32_CYTRON_MD* _Mx[], QEI* _encx[], PID_CONTROLLER* _pidx_pos[], PID_CONTROLLER* _pidx_vel[], DC_MOTOR_FFD* _ffdx[], KalmanFilter* _kfx[], MotionGenerator* _tpx[], Homing_controller* _hcx[], Adafruit_MCP23X17& mcpRef)
  : mcp(mcpRef) {
  for (int i = 0; i < NUM_MOTORS; ++i) {
    Mx[i] = _Mx[i];
    encx[i] = _encx[i];
    pidx_pos[i] = _pidx_pos[i];
    pidx_vel[i] = _pidx_vel[i];
    ffdx[i] = _ffdx[i];
    kfx[i] = _kfx[i];
    tpx[i] = _tpx[i];
    hcx[i] = _hcx[i];
  }
}

void Manipulator_command::begin() {
  for (int i = 0; i < NUM_MOTORS; i++) {
    encx[i]->begin();
  }

  Wire.begin(MCP_SDA, MCP_SCL);
  if (!mcp.begin_I2C()) {
    Serial.println("################ MCP Error ################");
    delay(1000);
    ESP.restart();
  } else {
    Serial.println("MCP Init OK");
  }

  pinMode(10, OUTPUT);          // H brake: OUTPUT
  digitalWrite(10, LOW);        // H brake

  mcp.pinMode(0, OUTPUT);       // GPA0: OUTPUT
  mcp.pinMode(1, OUTPUT);       // GPA1: OUTPUT
  mcp.pinMode(2, OUTPUT);       // GPA2: OUTPUT

  // Wire1.begin(BNO_SDA, BNO_SCL, 400000);
  // while (!bno.begin()) vTaskDelay(10 / portTICK_PERIOD_MS);
  // bno.setExtCrystalUse(true);

  delay(100);

  for (int i = 0; i < NUM_MOTORS; i++) {
    Mx[i]->begin();
    Mx[i]->set_duty(0);

    cmd_ux[i] = 0;
    fb_q[i] = 0;
    fb_qd[i] = 0;
    fb_i[i] = 0;

    encx[i]->reset();
    kfx[i]->begin();
    // hcx[i]->attachMotor([this, i](float speed) {
    //   Mx[i]->set_duty(speed);
    // });

    //   // tpx[i]->update(0);
  }

  // for (int i = 1; i < NUM_MOTORS; i++) {
  //   hcx[i]->setTripCurrent(MOTOR_X_CURRENT_LIMIT);
  //   hcx[i]->attachCompleteCallback([this, i]() {
  //     this->home_count++;
  //   });
  // }

  delay(100);
}

void Manipulator_command::setGoal(uint8_t M_index, float targetPosition) {
  static float dof0_prev_targetpos = -1;
  static bool break_flag = false;

  if (M_index == 0 && dof0_prev_targetpos != targetPosition && break_flag == true) {
    dof0_prev_targetpos = targetPosition;
    break_flag = false;
    digitalWrite(10, LOW);
  }

  q_target[M_index] = tpx[M_index]->update(targetPosition);
  qd_target[M_index] = tpx[M_index]->getVelocity();

  fb_q[M_index] += encx[M_index]->get_diff_count() * 2 * M_PI / ppr[M_index];
  float* kf_ptr = kfx[M_index]->Compute(fb_q[M_index], cmd_ux[M_index] * ffdx[M_index]->Vmax / ffdx[M_index]->Umax);

  fb_qd[M_index] = kf_ptr[1];
  fb_i[M_index] = kf_ptr[3];

  if (abs(targetPosition - fb_q[M_index]) > 0.01) {

    cmd_vx[M_index] = pidx_pos[M_index]->Compute(q_target[M_index] - fb_q[M_index]);

    if (qd_target[M_index] + cmd_vx[M_index] != 0) {

      cmd_ux[M_index] = PWM_Satuation(pidx_vel[M_index]->Compute(qd_target[M_index] + cmd_vx[M_index] - fb_qd[M_index]) + ffdx[M_index]->Compute(qd_target[M_index], i_gain[M_index] * fb_i[M_index]), ffdx[M_index]->Umax, -1 * ffdx[M_index]->Umax);

    } else {
      cmd_ux[M_index] = 0;
    }
  } else {
    if (M_index == 0) break_flag = true;
    cmd_ux[M_index] = 0;
  }

  if (M_index == 0 && break_flag == true) {
    digitalWrite(10, HIGH);
    cmd_ux[M_index] = 0;
  }

  Mx[M_index]->set_duty(cmd_ux[M_index]);
}

void Manipulator_command::tunesetGoal(uint8_t M_index, float target) {

  //q_target[M_index] = tpx[M_index]->update(target);
  qd_target[M_index] = target;


  fb_q[M_index] += encx[M_index]->get_diff_count() * 2 * M_PI / encx[M_index]->pulse_per_rev;

  float* kf_ptr = kfx[M_index]->Compute(fb_q[M_index], cmd_ux[M_index] * ffdx[M_index]->Vmax / ffdx[M_index]->Umax);
  //float* kf_ptr = kfx[M_index]->Compute(fb_q[M_index], target * ffdx[M_index]->Vmax / ffdx[M_index]->Umax);

  fb_qd[M_index] = kf_ptr[1];
  fb_i[M_index] = kf_ptr[3];

  if (qd_target[M_index] != 0) {
    cmd_ux[M_index] = PWM_Satuation(pidx_vel[M_index]->Compute(qd_target[M_index] - fb_qd[M_index]), ffdx[M_index]->Umax, -1 * ffdx[M_index]->Umax);  //+ ffdx[M_index]->Compute(qd_target[M_index], CURRENT_GAIN * fb_i[M_index])
  } else {
    cmd_ux[M_index] = 0;
  }

  Mx[M_index]->set_duty(target);  //cmd_ux[M_index]
}

void Manipulator_command::setHome(uint8_t M_index) {
  hcx[M_index]->home();
}

void Manipulator_command::setHomeAll() {
  for (int i = 1; i < NUM_MOTORS; i++) {
    hcx[i]->home();
    // Mx[i]->set_duty(-5000);
  }
}

void Manipulator_command::pollHoming() {
  while(this->home_count < 3) {
    Serial.print(this->home_count);
    Serial.println("/3 homing successfully.");
    for (int i = 1; i < NUM_MOTORS; i++) {
      hcx[i]->poll_for_status(fb_i[i]);
    }
  }

  // homing finished
  // encx[1]->reset();
  // encx[2]->reset();
  // encx[3]->reset();
}

void Manipulator_command::magnet(uint8_t ID, bool state) {
  uint8_t pins[3] = {0, 1, 2};
  mcp.digitalWrite(pins[ID], state);
}
