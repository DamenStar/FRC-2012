#ifndef SUBSYSTEMS_DRIVE_H_
#define SUBSYSTEMS_DRIVE_H_

#include "WPILib.h"

#include "config/Constants.h"

/**
 * @author Eric Caldwell
 * @author Bhargava Manja
 * Easy-access functions for drive functions: setting power, getting encoder values, etc.
 */
class Drive {
 public:
  /**
   * Constructor
   * Accepts the Victors and Encoders to get and set values
   */
  Drive(Victor* leftVictorA, Victor* leftVictorB, Victor* rightVictorA, Victor* rightVictorB,
        Encoder* leftEncoder, Gyro* gyro);

  /**
   * Sets power to the left and right sides of the drivetrain
   * @param left left power
   * @param right right power
   */
  void SetPower(double left, double right);

  /**
   * Sets the power to the left wheel
   * Automatically caps the power to +1.0 or -1.0
   * @param power the power to set
   */
  void SetLeftDrivePower(double power);

  /**
   * Sets the power to the left wheel
   * Automatically caps the power to +1.0 or -1.0
   * @param power the power to set
   */
  void SetRightDrivePower(double power);

  /**
   * Gets the distance travelled by the left side of the robot in meters
   * Calculated via the wheel circumference, gear ratio, and encoder return value
   * @param power the power to set
   */
  double GetLeftEncoderDistance();
  
  /**
   * Returns current gyro angle. Gyro is reset after instance is created
   */
  double GetGyroAngle();

  /**
   * Sets gyro's sensitivity. Input is in V/sec, so remember to divide argument
   * by 1000 if spec sheet says mV, which is most common
   */
  void SetGyroSensitivity(double sensitivity);

  /**
   * Resets gyro so that current angle becomes new 0 degrees. Makes sequential turns 
   * easier. Also required to make sensor values accurate after noise is encountered 
   */
  void ResetGyro();
 private:

  // Victors

  Victor* leftDriveMotorA_;
  Victor* leftDriveMotorB_;
  Victor* rightDriveMotorA_;
  Victor* rightDriveMotorB_;

  // Sensors
  Encoder* leftDriveEncoder_;
  Gyro* gyro_;
  Constants* constants;
};

#endif  // SUBSYSTEMS_DRIVE_H_
