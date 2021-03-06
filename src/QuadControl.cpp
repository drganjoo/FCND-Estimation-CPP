#include "Common.h"
#include "QuadControl.h"

#include "Utility/SimpleConfig.h"

#include "Utility/StringUtils.h"
#include "Trajectory.h"
#include "BaseController.h"
#include "Math/Mat3x3F.h"

//#define CASCADED

#ifdef __PX4_NUTTX
#include <systemlib/param/param.h>
#endif

void QuadControl::Init()
{
  BaseController::Init();

  // variables needed for integral control
  integratedAltitudeError = 0;
    
#ifndef __PX4_NUTTX
  // Load params from simulator parameter system
  ParamsHandle config = SimpleConfig::GetInstance();
   
  // Load parameters (default to 0)
  kpPosXY = config->Get(_config+".kpPosXY", 0);
  kpPosZ = config->Get(_config + ".kpPosZ", 0);
  KiPosZ = config->Get(_config + ".KiPosZ", 0);
     
  kpVelXY = config->Get(_config + ".kpVelXY", 0);
  kpVelZ = config->Get(_config + ".kpVelZ", 0);

  kpBank = config->Get(_config + ".kpBank", 0);
  kpYaw = config->Get(_config + ".kpYaw", 0);

  kpPQR = config->Get(_config + ".kpPQR", V3F());

  maxDescentRate = config->Get(_config + ".maxDescentRate", 100);
  maxAscentRate = config->Get(_config + ".maxAscentRate", 100);
  maxSpeedXY = config->Get(_config + ".maxSpeedXY", 100);
  maxAccelXY = config->Get(_config + ".maxHorizAccel", 100);

  maxTiltAngle = config->Get(_config + ".maxTiltAngle", 100);

  minMotorThrust = config->Get(_config + ".minMotorThrust", 0);
  maxMotorThrust = config->Get(_config + ".maxMotorThrust", 100);
#else
  // load params from PX4 parameter system
  //TODO
  param_get(param_find("MC_PITCH_P"), &Kp_bank);
  param_get(param_find("MC_YAW_P"), &Kp_yaw);
#endif
}

VehicleCommand QuadControl::GenerateMotorCommands(float collThrustCmd, V3F momentCmd)
{
  // Convert a desired 3-axis moment and collective thrust command to 
  //   individual motor thrust commands
  // INPUTS: 
  //   collThrustCmd: desired collective thrust [N]
  //   momentCmd: desired rotation moment about each axis [N m]
  // OUTPUT:
  //   set class member variable cmd (class variable for graphing) where
  //   cmd.desiredThrustsN[0..3]: motor commands, in [N]

  // HINTS: 
  // - you can access parts of momentCmd via e.g. momentCmd.x
  // You'll need the arm length parameter L, and the drag/thrust ratio kappa

  ////////////////////////////// BEGIN STUDENT CODE ///////////////////////////
	// ref: https://www.overleaf.com/read/thzntmhcqkkp#/63267348/
	// and Motor Control.pdf from JID

	ParamsHandle config = SimpleConfig::GetInstance();
	float L = config->Get(_config + ".L", 0);
	float l = L / sqrt(2);
	float kappa = config->Get(_config + ".kappa", 0);

	// Combined thrust from all of the motors is given as collThrustCmd. For simultaneous equation to be solved the following is the formula:
	// c = F_1 + F_2 + F_3 + F_4

	// Moment around the X-Axis would be roll. This would be defined by the thrust generated by F1 + F3 (2 rotors on the left) combined subtracted 
	// from F2 and F4 (the two rotors on the right)
	// Mx = (F_1 - F_2 + F_3 - F_4) * l

	// Moment around the Y-Axis would be the pitch. This would be defined by the thrust generated by F1 + F2 (2 rotors in the front) combined subtracted 
	// from F3 and F4 (the two rotors on the back)
	// My = (F_1 + F_2 - F_3 - F_4) * l

	// Moment around the z axis is the yaw. This would be defined by the clockwise motors combines (f1 and f4) subtracted from the anticlockwise
	// motors (F2 and F3)
	// Mz = M1 - M2 - M3 + M4

	// Using kappa, we can find out Thrust from Moment. moment = kappa * thrust

	float a = collThrustCmd;
	float b = momentCmd.x / l;
	float c = momentCmd.y / l;
	float d = momentCmd.z / kappa;

	// use simultaneous equation solver to find the equation for each individual motor thrust
	// https://www.wolframalpha.com/input/?i=%7B%7B1,1,1,1%7D,%7B1,-1,1,-1%7D,%7B1,1,-1,-1%7D,%7B-1,1,1,-1%7D%7D+*+%7BF0,F1,F2,F3%7D+%3D+%7BA,B,C,D%7D
	cmd.desiredThrustsN[0] = 0.25 * (a + b + c - d);
	cmd.desiredThrustsN[1] = 0.25 * (a - b + c + d);
	cmd.desiredThrustsN[2] = 0.25 * (a + b - c + d);
	cmd.desiredThrustsN[3] = 0.25 * (a - b - c - d);

  /////////////////////////////// END STUDENT CODE ////////////////////////////

  return cmd;
}

V3F QuadControl::BodyRateControl(V3F pqrCmd, V3F pqr)
{
  // Calculate a desired 3-axis moment given a desired and current body rate
  // INPUTS: 
  //   pqrCmd: desired body rates [rad/s]
  //   pqr: current or estimated body rates [rad/s]
  // OUTPUT:
  //   return a V3F containing the desired moments for each of the 3 axes

  // HINTS: 
  //  - you can use V3Fs just like scalars: V3F a(1,1,1), b(2,3,4), c; c=a-b;
  //  - you'll need parameters for moments of inertia Ixx, Iyy, Izz
  //  - you'll also need the gain parameter kpPQR (it's a V3F)

  V3F momentCmd;

  ////////////////////////////// BEGIN STUDENT CODE ///////////////////////////

	V3F error = pqrCmd - pqr;
	V3F p_term = kpPQR * error;

	// moment = I * a
	V3F I(Ixx, Iyy, Izz);
	momentCmd = I * p_term;

  /////////////////////////////// END STUDENT CODE ////////////////////////////

  return momentCmd;
}

// returns a desired roll and pitch rate 
V3F QuadControl::RollPitchControl(V3F accelCmd, Quaternion<float> attitude, float collThrustCmd)
{
  // Calculate a desired pitch and roll angle rates based on a desired global
  //   lateral acceleration, the current attitude of the quad, and desired
  //   collective thrust command
  // INPUTS: 
  //   accelCmd: desired acceleration in global XY coordinates [m/s2]
  //   attitude: current or estimated attitude of the vehicle
  //   collThrustCmd: desired collective thrust of the quad [N]
  // OUTPUT:
  //   return a V3F containing the desired pitch and roll rates. The Z
  //     element of the V3F should be left at its default value (0)

  // HINTS: 
  //  - we already provide rotation matrix R: to get element R[1,2] (python) use R(1,2) (C++)
  //  - you'll need the roll/pitch gain kpBank
  //  - collThrustCmd is a force in Newtons! You'll likely want to convert it to acceleration first

  V3F pqrCmd;
  Mat3x3F R = attitude.RotationMatrix_IwrtB();

  ////////////////////////////// BEGIN STUDENT CODE ///////////////////////////

	float collAccel = collThrustCmd / mass;

	// make sure we only look for positive collective thrust and don't do anything for
	// negative thrust

	if (collAccel > 0) {

		// reference 4.1/4.2 of Lesson 14 - 3D Drone Full Notebook exercise
		// we need to do the calculation in body frame where as the incoming accelCmd
		// is in the inertial frame
		float bx_c = -accelCmd.x / collAccel;
		float by_c = -accelCmd.y / collAccel;
		float bx_a = R(0, 2);
		float by_a = R(1, 2);

		bx_c = CONSTRAIN(bx_c, -maxTiltAngle, maxTiltAngle);
		by_c = CONSTRAIN(by_c, -maxTiltAngle, maxTiltAngle);

		float bx_c_dot = kpBank * (bx_c - bx_a);
		float by_c_dot = kpBank * (by_c - by_a);

		float r33 = 1 / R(2, 2);

		pqrCmd.x = r33 * (R(1, 0) * bx_c_dot - R(0, 0) * by_c_dot);
		pqrCmd.y = r33 * (R(1, 1) * bx_c_dot - R(0, 1) * by_c_dot);
		pqrCmd.z = 0.0;
	}
	else {
		pqrCmd.x = 0;
		pqrCmd.y = 0;
		pqrCmd.z = 0;
	}

  /////////////////////////////// END STUDENT CODE ////////////////////////////

  return pqrCmd;
}

float QuadControl::AltitudeControl(float posZCmd, float velZCmd, float posZ, float velZ, Quaternion<float> attitude, float accelZCmd, float dt)
{
  // Calculate desired quad thrust based on altitude setpoint, actual altitude,
  //   vertical velocity setpoint, actual vertical velocity, and a vertical 
  //   acceleration feed-forward command
  // INPUTS: 
  //   posZCmd, velZCmd: desired vertical position and velocity in NED [m]
  //   posZ, velZ: current vertical position and velocity in NED [m]
  //   accelZCmd: feed-forward vertical acceleration in NED [m/s2]
  //   dt: the time step of the measurements [seconds]
  // OUTPUT:
  //   return a collective thrust command in [N]

  // HINTS: 
  //  - we already provide rotation matrix R: to get element R[1,2] (python) use R(1,2) (C++)
  //  - you'll need the gain parameters kpPosZ and kpVelZ
  //  - maxAscentRate and maxDescentRate are maximum vertical speeds. Note they're both >=0!
  //  - make sure to return a force, not an acceleration
  //  - remember that for an upright quad in NED, thrust should be HIGHER if the desired Z acceleration is LOWER

  Mat3x3F R = attitude.RotationMatrix_IwrtB();
  float thrust = 0;

  ////////////////////////////// BEGIN STUDENT CODE ///////////////////////////

	//float hdot_cmd = kpPosZ * (posZCmd - posZ) + velZCmd;
	// hdot_cmd = CONSTRAIN(hdot_cmd, -maxDescentRate, maxAscentRate);
	// float accel_cmd = accelZCmd  + kpVelZ * (hdot_cmd - velZ);
	// accel_cmd = CONSTRAIN(accel_cmd, -maxDescentRate, maxAscentRate);


#ifdef CASCADED
  float error_pos_z = posZCmd - posZ;
  integratedAltitudeError += error_pos_z * dt;

  velZCmd += kpPosZ * error_pos_z;
  velZCmd = CONSTRAIN(velZCmd, -maxAscentRate, maxDescentRate);

  float i = KiPosZ * integratedAltitudeError;
  float ff = accelZCmd;

  float accel = kpVelZ * (velZCmd - velZ) + i + ff - CONST_GRAVITY;
  thrust = -accel / R(2,2) * mass;
#else
  float error_pos_z = posZCmd - posZ;
  float error_vel_z = velZCmd - velZ;

	integratedAltitudeError += error_pos_z * dt;

	float p = kpPosZ * error_pos_z;
	float d = kpVelZ * error_vel_z;
	float i = KiPosZ * integratedAltitudeError;

	float u = accelZCmd + p + i + d;
	float acceleration = (u - CONST_GRAVITY) / R(2, 2);
	acceleration = CONSTRAIN(acceleration, -maxDescentRate / dt, maxAscentRate / dt);

	thrust = -mass * acceleration;
#endif
  /////////////////////////////// END STUDENT CODE ////////////////////////////
  
  return thrust;
}

// returns a desired acceleration in global frame
V3F QuadControl::LateralPositionControl(V3F posCmd, V3F velCmd, V3F pos, V3F vel, V3F accelCmdFF)
{
  // Calculate a desired horizontal acceleration based on 
  //  desired lateral position/velocity/acceleration and current pose
  // INPUTS: 
  //   posCmd: desired position, in NED [m]
  //   velCmd: desired velocity, in NED [m/s]
  //   pos: current position, NED [m]
  //   vel: current velocity, NED [m/s]
  //   accelCmdFF: feed-forward acceleration, NED [m/s2]
  // OUTPUT:
  //   return a V3F with desired horizontal accelerations. 
  //     the Z component should be 0
  // HINTS: 
  //  - use the gain parameters kpPosXY and kpVelXY
  //  - make sure you limit the maximum horizontal velocity and acceleration
  //    to maxSpeedXY and maxAccelXY

  // make sure we don't have any incoming z-component
  accelCmdFF.z = 0;
  velCmd.z = 0;
  posCmd.z = pos.z;

  // we initialize the returned desired acceleration to the feed-forward value.
  // Make sure to _add_, not simply replace, the result of your controller
  // to this variable
  V3F accelCmd = accelCmdFF;

  ////////////////////////////// BEGIN STUDENT CODE ///////////////////////////

#ifdef CASCADED
// ref:

//	float vel_norm = velCmd.magXY();
//	if (vel_norm > maxSpeedXY) {
//		velCmd *= maxSpeedXY / vel_norm;
//	}
//
//	V3F delta_xy = posCmd - pos;
//	V3F delta_vel_xy = velCmd - vel;
//
//	V3F term1 = kpPosXY * delta_xy;
//	V3F term2 = kpVelXY * (term1 + delta_vel_xy);
//
//	// make sure z component is not effected
//	term1.z = 0;
//	term2.z = 0;
//
//	accelCmd = accelCmdFF + term1 + term2;
//	assert(accelCmd.z == 0);
	// ref: https://www.overleaf.com/read/bgrkghpggnyc#/61023787/

	// had there been no constraints, we would have implemented the classical
	// definition of cascaded P controller:
	// V3F double_p = kpVelXY * (kpPosXY * (posCmd - pos) - vel);

	V3F error = posCmd - pos;
  velCmd += kpPosXY * error;

	float vel_norm = velCmd.magXY();
	if (vel_norm > maxSpeedXY) {
		velCmd *= maxSpeedXY / vel_norm;
	}

	accelCmd = accelCmdFF + kpVelXY * (velCmd - vel);
	assert(accelCmd.z == 0);
#else
	// constrain required velocity to the maximum allowed velocity
	float vel_norm = velCmd.magXY();
	if (vel_norm > maxSpeedXY) {
		velCmd *= maxSpeedXY / vel_norm;
	}

	V3F error_pos = posCmd - pos;
	V3F error_v = velCmd - vel;

	V3F term1 = kpPosXY * error_pos;
	V3F term2 = kpVelXY * error_v;

	// make sure z component is not effected
	term1.z = 0;
	term2.z = 0;

	accelCmd = accelCmdFF + term1 + term2;
	assert(accelCmd.z == 0);
#endif

  //// make sure we don't accelerate > the max acceleration allowed
  float accelNorm = accelCmd.magXY();
  if (accelNorm > maxAccelXY) {
    accelCmd *= maxAccelXY / accelNorm;
  }

  /////////////////////////////// END STUDENT CODE ////////////////////////////

  return accelCmd;
}

// returns desired yaw rate
float QuadControl::YawControl(float yawCmd, float yaw)
{
  // Calculate a desired yaw rate to control yaw to yawCmd
  // INPUTS: 
  //   yawCmd: commanded yaw [rad]
  //   yaw: current yaw [rad]
  // OUTPUT:
  //   return a desired yaw rate [rad/s]
  // HINTS: 
  //  - use fmodf(foo,b) to unwrap a radian angle measure float foo to range [0,b]. 
  //  - use the yaw control gain parameter kpYaw

  float yawRateCmd=0;
  ////////////////////////////// BEGIN STUDENT CODE ///////////////////////////

	yawCmd = CONSTRAIN(yawCmd, -2.0 * M_PI, 2.0 * M_PI);
	float yaw_error = yawCmd - yaw;

	if (yaw_error > M_PI)
		yaw_error = yaw_error - 2.0 * M_PI;
	else if (yaw_error < -M_PI)
		yaw_error = yaw_error + 2.0 * M_PI;

	yawRateCmd = kpYaw * yaw_error;

  /////////////////////////////// END STUDENT CODE ////////////////////////////

  return yawRateCmd;

}

VehicleCommand QuadControl::RunControl(float dt, float simTime)
{
  curTrajPoint = GetNextTrajectoryPoint(simTime);

  float collThrustCmd = AltitudeControl(curTrajPoint.position.z, curTrajPoint.velocity.z, estPos.z, estVel.z, estAtt, curTrajPoint.accel.z, dt);

  // reserve some thrust margin for angle control
  float thrustMargin = .1f*(maxMotorThrust - minMotorThrust);
  collThrustCmd = CONSTRAIN(collThrustCmd, (minMotorThrust+ thrustMargin)*4.f, (maxMotorThrust-thrustMargin)*4.f);
  
  V3F desAcc = LateralPositionControl(curTrajPoint.position, curTrajPoint.velocity, estPos, estVel, curTrajPoint.accel);
  
  V3F desOmega = RollPitchControl(desAcc, estAtt, collThrustCmd);
  desOmega.z = YawControl(curTrajPoint.attitude.Yaw(), estAtt.Yaw());

  V3F desMoment = BodyRateControl(desOmega, estOmega);

	//#define CONFIRM_MOTOR_CMDS
	//#ifdef CONFIRM_MOTOR_CMDS
	//	ParamsHandle config = SimpleConfig::GetInstance();
	//	float L = config->Get(_config + ".L", 0);
	//	float l = L / sqrt(2);
	//
	//	//collThrustCmd = mass * CONST_GRAVITY;
	//	desMoment.x = 0;
	//	//desMoment.x = 10.0 / 180.0 * M_PI * l;
	//	desMoment.y = 0;
	//	desMoment.y = 10.0 / 180.0 * M_PI * l;
	//	desMoment.z = 0;
	//	//desMoment.z = -10.0/180.0 * M_PI * l;
	//#endif

  return GenerateMotorCommands(collThrustCmd, desMoment);
}
