#include "esp32-hal-gpio.h"
#include "rom/spi_flash.h"
#include "Manipulator_Config.h"
#include "Wire.h"
#include "esp32-hal.h"
#include "Manipulator_command.h"

Manipulator_command::Manipulator_command(ESP32_CYTRON_MD* _Mx[], QEI* _encx[], PID_CONTROLLER* _pidx_pos[], PID_CONTROLLER* _pidx_vel[], DC_MOTOR_FFD* _ffdx[], KalmanFilter* _kfx[], MotionGenerator* _tpx[]) {
  for (int i = 0; i < NUM_MOTORS; ++i) {
    Mx[i] = _Mx[i];
    encx[i] = _encx[i];
    pidx_pos[i] = _pidx_pos[i];
    pidx_vel[i] = _pidx_vel[i];
    ffdx[i] = _ffdx[i];
    kfx[i] = _kfx[i];
    tpx[i] = _tpx[i];
  }
}

void Manipulator_command::begin() {
  for (int i = 0; i < NUM_MOTORS; i++) {
    encx[i]->begin();
  }

  pinMode(10, OUTPUT);          // H brake: OUTPUT
  digitalWrite(10, LOW);        // H brake

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
  }
  
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
