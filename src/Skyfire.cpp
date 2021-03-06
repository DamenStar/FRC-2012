#include "skyfire.h"

#include <cmath>
#include <cstdio>

//#include "auto/AutoAlignCommand.h"
#include "auto/AutoMacros.h"
#include "auto/BridgeBallsCommand.h"
#include "auto/SetIntakePositionCommand.h"
#include "auto/IntakeCommand.h"
#include "auto/ShootFieldCommand.h"
#include "auto/ConcurrentCommand.h"
#include "auto/DriveCommand.h"
#include "auto/DelayCommand.h"
#include "auto/OldDriveCommand.h"
#include "auto/JumbleCommand.h"
#include "auto/SequentialCommand.h"
#include "auto/ShootCommand.h"
#include "auto/TurnCommand.h"
#include "auto/QueueBallCommand.h"
#include "auto/AutoAlignCommand.h"
#include "auto/AutoShootCommand.h"
#include "auto/SetWheelSpeedCommand.h"
#include "drivers/AutoTurnDriver.h"
#include "drivers/BaselockDriver.h"
#include "drivers/Driver.h"
#include "drivers/TeleopDriver.h"
#include "subsystems/Drive.h"
#include "subsystems/Intake.h"
#include "subsystems/OperatorControl.h"
#include "subsystems/Shooter.h"
#include "util/Functions.h"
#include "util/Logger.h"
#include "util/PidTuner.h"
#include "util/RelativeGyro.h"
#include "vision/BackboardFinder.h"
#include "auto/ShootFromBridgeCommand.h"

Logger* voltageLogger = new Logger("/matchLog.log");
AnalogChannel* voltage = new AnalogChannel(2);
AnalogChannel* radioV = new AnalogChannel(3);
Logger* autoLogger = new Logger("/timeLog.log");
Logger* teleopLogger = new Logger("/teleopLog.log");

Skyfire::Skyfire() {
  SetPeriod(0.02);
  constants_ = Constants::GetInstance();

  // Motors
  leftDriveMotorA_ = new Victor((int)constants_->leftDrivePwmA);
  leftDriveMotorB_ = new Victor((int)constants_->leftDrivePwmB);
  rightDriveMotorA_ = new Victor((int)constants_->rightDrivePwmA);
  rightDriveMotorB_ = new Victor((int)constants_->rightDrivePwmB);
  intakeMotor1_ = new Victor((int)constants_->intakePwm1);
  intakeMotor2_ = new Victor((int)constants_->intakePwm2);
  intakeMotor3_ = new Victor((int)constants_->intakePwm3);
  conveyorMotor_ = new Victor((int)constants_->conveyorPwm);
  leftShooterMotor_ = new Victor((int)constants_->leftShooterPwm);
  rightShooterMotor_ = new Victor((int)constants_->rightShooterPwm);

  // Sensors
  leftEncoder_ = new Encoder((int)constants_->leftEncoderPortA, (int)constants_->leftEncoderPortB);
  leftEncoder_->Start();
  rightEncoder_ = new Encoder((int)constants_->rightEncoderPortA, (int)constants_->rightEncoderPortB);
  rightEncoder_->Start();
  shooterEncoder_ = new Encoder((int)constants_->shooterEncoderPortA, (int)constants_->shooterEncoderPortB,
                                false, CounterBase::k4X);
  shooterEncoder_->Start();
  gyro_ = new RelativeGyro((int)constants_->gyroPort);
  bumpSensor_ = new DigitalInput((int)constants_->bumpSensorPort);
  conveyorBallSensor_ = new AnalogChannel((int)constants_->conveyorBallSensorPort);
  poofMeter_ = new AnalogChannel((int)constants_->poofMeterPort);
  ballRanger_ = new AnalogChannel((int)constants_->ballRangerPort);

  // Pneumatics
  compressor_ = new Compressor((int)constants_->compressorPressureSwitchPort,
                               (int)constants_->compressorRelayPort);
  compressor_->Start();
  shiftSolenoid_ = new Solenoid((int)constants_->shiftSolenoidPort);
  hoodSolenoid_ = new Solenoid((int)constants_->hoodSolenoidPort);
  pizzaWheelSolenoid_ = new Solenoid((int)constants_->pizzaWheelSolenoidDownPort);
  intakeUpSolenoid_ = new Solenoid((int)constants_->intakeSolenoidUpPort);
  intakeDownSolenoid_ = new Solenoid((int)constants_->intakeSolenoidDownPort);
  brakeSolenoid_ = new DoubleSolenoid((int)constants_->brakeSolenoidOnPort,
                                      (int)constants_->brakeSolenoidOffPort);
  dingusSolenoid_ = new Solenoid((int)constants_->dingusSolenoidPort);

  // Subsystems
  drivebase_ = new Drive(leftDriveMotorA_, leftDriveMotorB_, rightDriveMotorA_, rightDriveMotorB_,
                         shiftSolenoid_, pizzaWheelSolenoid_, brakeSolenoid_,  leftEncoder_, rightEncoder_,
                         gyro_, bumpSensor_);
  intake_ = new Intake(intakeMotor1_, intakeMotor2_, intakeMotor3_, intakeUpSolenoid_, intakeDownSolenoid_);
  shooter_ = new Shooter(conveyorMotor_, leftShooterMotor_, rightShooterMotor_, shooterEncoder_,
                         hoodSolenoid_, conveyorBallSensor_, poofMeter_, ballRanger_);
  shooterTargetVelocity_ = 0;

  // Control board
  leftJoystick_ = new Joystick((int)constants_->leftJoystickPort);
  rightJoystick_ = new Joystick((int)constants_->rightJoystickPort);
  operatorControl_ = new OperatorControl((int)constants_->operatorControlPort);

  // Vision
  target_ = new BackboardFinder(drivebase_);
  target_->Start();

  // Drivers
  teleopDriver_ = new TeleopDriver(drivebase_, leftJoystick_, rightJoystick_, operatorControl_);
  baselockDriver_ = new BaselockDriver(drivebase_, leftJoystick_, rightJoystick_);
  autoAlignDriver_ = new AutoTurnDriver(drivebase_, target_);
  currDriver_ = teleopDriver_;

  // Watchdog
  GetWatchdog().SetExpiration(1000);
  GetWatchdog().SetEnabled(true);

  // Get a local instance of the Driver Station LCD
  lcd_ = DriverStationLCD::GetInstance();

  // Initialize button edge-detection variables
  oldAutoAlignButton_ = leftJoystick_->GetRawButton((int)constants_->autoAlignPort);
  oldShooterSwitch_ = operatorControl_->GetShooterSwitch();
  oldIncreaseButton_ = operatorControl_->GetIncreaseButton();
  oldDecreaseButton_ = operatorControl_->GetDecreaseButton();
  oldAutonSelectButton_ = operatorControl_->GetAutonSelectButton();
  oldUnjamButton_ = operatorControl_->GetUnjamButton();
  oldHardUpButton_ = operatorControl_->GetKeyFarButton();
  oldHardDownButton_ = operatorControl_->GetKeyCloseButton();
  reverseConveyor_ = 0;

  // Initialize autonomous variables
  autonDelay_ = 0.0;
  autonTimer_ = new Timer();
  autonMode_ = AUTON_2_PLUS_2;
  autonBias_ = BIAS_NONE;
  ballHardness_ = BALL_DEFAULT;
  autoBaseCmd_ = NULL;
  timer_ = new Timer();
  timer_->Start();

  prevLeftDist_ = 0.0;
  prevRightDist_ = 0.0;
  prevTime = 0.0;
  shooterIncr_ = 0.0;

  lightDelay_ = new Timer();
  autonSafetyTimer_ = new Timer();
  autonSafetyTimer_->Start();
  autonRanOnce_ = false;

  shooterControl_ = new Notifier(Shooter::CallUpdate, shooter_);
  shooterControl_->StartPeriodic(1.0 / 50.0);

}

void Skyfire::ResetMotors() {
  drivebase_->SetLinearPower(0, 0);
  shooter_->SetLinearPower(0);
  shooter_->SetLinearConveyorPower(0);
  intake_->SetIntakePower(0);
}

void Skyfire::DisabledInit() {
  drivebase_->ResetEncoders();
  drivebase_->ResetGyro();
}

void Skyfire::AutonomousInit() {

  if (autonSafetyTimer_->Get() < 5.0 && autonRanOnce_) {
    return;
  }
  autonRanOnce_ = true;

  constants_->LoadFile();
  ResetMotors();

  autonTimer_->Reset();
  autonTimer_->Start();
  intake_->SetIntakePosition(Intake::INTAKE_UP);
  shooter_->SetTargetVelocity(0);

  if (autoBaseCmd_) {
    delete autoBaseCmd_;
    autoBaseCmd_ = NULL;
  }

  double autonBiasTurn = 0;
  double allianceBridgeAngle = 13.0;
  if (autonBias_ == BIAS_LEFT) {
    autonBiasTurn = -10;
    allianceBridgeAngle -= 1;
  } else if (autonBias_ == BIAS_RIGHT) {
    autonBiasTurn = 10;
    allianceBridgeAngle += 1;
  }

  switch (ballHardness_) {
    case BALL_WTF_SOFT:
      shooter_->SetHardnessOffset(constants_->wtfSoftBallOffset);
      break;
    case BALL_SOFT:
      shooter_->SetHardnessOffset(constants_->softBallOffset);
      break;
    case BALL_DEFAULT:
      shooter_->SetHardnessOffset(0);
      break;
    case BALL_HARD:
      shooter_->SetHardnessOffset(constants_->hardBallOffset);
      break;
    case BALL_WTF_HARD:
      shooter_->SetHardnessOffset(constants_->wtfHardBallOffset);
      break;
    default:
      shooter_->SetHardnessOffset(0);
      break;
  }

  switch (autonMode_) {
    case AUTON_NONE:
      break;

    // Auto distance shooting
    case AUTON_START_ANYWHERE:
      autoBaseCmd_ = AUTO_SEQUENTIAL(
        new AutoAlignCommand(drivebase_, new AutoTurnDriver(drivebase_, target_), target_, 0, 3.0),
        new AutoShootCommand(shooter_, intake_, target_, true, 2, 6.0));
      break;

    // Start at front center of key. Shoot 2 from fender
    case AUTON_FENDER:
      autoBaseCmd_ = AUTO_SEQUENTIAL(
        new OldDriveCommand(drivebase_, -68, 0.0, false, 3, .7),
        AUTO_CONCURRENT(
          new OldDriveCommand(drivebase_, -25, 0.0, false, .5, .7),
          new ShootCommand(shooter_, intake_, true, 37.5, 2, 6.0)));
      break;

    // Shoot 1, go to bridge, gather, then drive back and shoot 3
    case AUTON_1_PLUS_3:
      // Intentional fall through!

    // Shoot 2, go to bridge, gather 2, then shoot them.
    case AUTON_2_PLUS_2:
      autoBaseCmd_ = AUTO_SEQUENTIAL(
        new ShootCommand(shooter_, intake_, true, constants_->shooterKeyFarSpeed, (autonMode_ == AUTON_1_PLUS_3) ? 1 : 2, 6.0),
        new OldDriveCommand(drivebase_, 55, autonBiasTurn * .65, false, 3),
        new OldDriveCommand(drivebase_, 25, -autonBiasTurn * .75, false, .8, .6),
        BALLS_FROM_BRIDGE_COMMAND(true),
        new SetWheelSpeedCommand(shooter_, constants_->shooterKeyFarSpeed - 6),
        new OldDriveCommand(drivebase_, -55, 0.0, false, 2.0),
        new DelayCommand(.25),
        AUTO_SHOOT_COMMAND(constants_->shooterKeyFarSpeed + .6, 0, false));
      break;

    // Immediately drive to bridge and tip.
    // Drive forward, shoot orignal 2.
    // Pick up other 2, shoot those
    case AUTON_0_PLUS_2_PLUS_2:
      autoBaseCmd_ = AUTO_SEQUENTIAL(
        new OldDriveCommand(drivebase_, 57, 0, false, 1.1),
        new SetIntakePositionCommand(intake_, Intake::INTAKE_DOWN),
        new OldDriveCommand(drivebase_, -7, 0, false, 1.0),
        new DelayCommand(.7),
        new SetWheelSpeedCommand(shooter_, constants_->shooterKeyFarSpeed),
        new OldDriveCommand(drivebase_, -45, 0.0, false, 2.0),
        new DelayCommand(.25),
        AUTO_CONCURRENT(
          new AutoAlignCommand(drivebase_, autoAlignDriver_, 0, 2.50, true),
          new ShootFieldCommand(shooter_, intake_, true, constants_->shooterKeyFarSpeed , 2, 4.0)),
        new IntakeCommand(intake_, shooter_),
        new OldDriveCommand(drivebase_, 50, 0, false, 2, .85),
        new OldDriveCommand(drivebase_, -50, 0, false, 1),
        new DelayCommand(.5),
        AUTO_SHOOT_COMMAND(constants_->shooterKeyFarSpeed, 0, false));
      break;

    // Shoot 2, drive to bridge, shoot from bridge
    case AUTON_SHOOT_FROM_BRIDGE:
      autoBaseCmd_ = AUTO_SEQUENTIAL(
        new ShootCommand(shooter_, intake_, true, constants_->shooterKeyFarSpeed, 2, 6.0),
        new OldDriveCommand(drivebase_, 56, 0.0 , false, 3),
        new OldDriveCommand(drivebase_, 30, 0.0, false, .5, .45),
        new DelayCommand(.5),
        new ShootFromBridgeCommand(shooter_, intake_, true, constants_->shooterBridgeSpeed, 10, 10));
      break;

    // Shoot 2 from front of key
    case AUTON_SHORT_SIMPLE:
      autoBaseCmd_ = AUTO_SEQUENTIAL (
        AUTO_CONCURRENT(
          AUTO_SEQUENTIAL(
            new SetIntakePositionCommand(intake_, Intake::INTAKE_DOWN),
            new DelayCommand(.5),
            new SetIntakePositionCommand(intake_, Intake::INTAKE_FLOATING)),
          new ShootCommand(shooter_, intake_, true, 46, 100, 100)));
      break;

    // Shoot 2 from back of key
    case AUTON_FAR_SIMPLE:
      autoBaseCmd_ = AUTO_SEQUENTIAL (
          new ShootCommand(shooter_, intake_, true,  constants_->shooterKeyFarSpeed, 2, 6.0));
      break;

    // Side. Start with back left wheel on corner of key.
    case AUTON_SIDE:
      autoBaseCmd_ = AUTO_SEQUENTIAL(
        new ShootCommand(shooter_, intake_, true, constants_->shooterKeyCloseSpeed, 2, 4),
        new SetWheelSpeedCommand(shooter_, 0));
      break;

    // Alliance Bridge. Start with front left wheel on right corner of key.
    case AUTON_ALLIANCE_BRIDGE:
      autoBaseCmd_ = AUTO_SEQUENTIAL(
        new ShootCommand(shooter_, intake_, true, Constants::GetInstance()->shooterKeyCloseSpeed + 3, 2, 4.5),
        new OldDriveCommand(drivebase_, 120, allianceBridgeAngle, false, 2.4),
        new OldDriveCommand(drivebase_, 100, -20, false, 1.0, .5),
        BALLS_FROM_BRIDGE_COMMAND(true),
        new SetWheelSpeedCommand(shooter_, constants_->shooterKeyCloseSpeed + 2),
        new OldDriveCommand(drivebase_, -50, 0, false, .3),
        new OldDriveCommand(drivebase_, -50, 7, false, .4),
        new OldDriveCommand(drivebase_, -50, 6, false, .4),
        new OldDriveCommand(drivebase_, -50, 6, false, .4),
        new OldDriveCommand(drivebase_, -35, 6, false, 1.5),
        AUTO_SHOOT_COMMAND(constants_->shooterKeyCloseSpeed + 3, 0, true));
      break;

    case AUTON_TEST:
      autoBaseCmd_ = AUTO_SEQUENTIAL(
        new DriveCommand(drivebase_, -100, 14, false, 4.0));
      break;

    default:
      autoBaseCmd_ = NULL;
  }

  if (autoBaseCmd_) {
    autoBaseCmd_->Initialize();
  }

  dingusSolenoid_->Set(false);
}

void Skyfire::TeleopInit() {
  constants_->LoadFile();
  ResetMotors();
  drivebase_->ResetGyro();
  drivebase_->ResetEncoders();
  prevLeftDist_ = 0.0;
  prevRightDist_ = 0.0;
  prevTime = 0.0;
  currDriver_ = teleopDriver_;
  currDriver_->Reset();
  timer_->Reset();
  shooterIncr_ = 0.0;
  switch (ballHardness_) {
    case BALL_WTF_SOFT:
      shooter_->SetHardnessOffset(constants_->wtfSoftBallOffset);
      break;
    case BALL_SOFT:
      shooter_->SetHardnessOffset(constants_->softBallOffset);
      break;
    case BALL_DEFAULT:
      shooter_->SetHardnessOffset(0);
      break;
    case BALL_HARD:
      shooter_->SetHardnessOffset(constants_->hardBallOffset);
      break;
    case BALL_WTF_HARD:
      shooter_->SetHardnessOffset(constants_->wtfHardBallOffset);
      break;
    default:
      shooter_->SetHardnessOffset(0);
      break;
  }
  dingusSolenoid_->Set(false);
  target_->SetUseSkew(true);
  lightDelay_->Start();
}

void Skyfire::DisabledPeriodic() {
  GetWatchdog().Feed();
  // Autonomous delay
  timer_->Reset();
  shooter_->Reset();
  shooter_->SetTargetVelocity(0);
  if (operatorControl_->GetIncreaseButton() && !oldIncreaseButton_) {
    autonDelay_ += 0.5;
  } else if (operatorControl_->GetDecreaseButton() && !oldDecreaseButton_) {
    autonDelay_ = max(autonDelay_ - 0.5, 0.0);
  }

  // Autonomous mode selection
  if (operatorControl_->GetAutonSelectButton() && !oldAutonSelectButton_) {
    autonMode_ = (AutonMode)(autonMode_ + 1);
    if (autonMode_ == NUM_AUTON_MODES) {
      autonMode_ = AUTON_NONE;
    }
  }

  // Autonomous bias selection
  if (operatorControl_->GetUnjamButton() && !oldUnjamButton_) {
    autonBias_ = (AutonBias)(autonBias_ + 1);
    if (autonBias_ == NUM_BIASES) {
      autonBias_ = BIAS_NONE;
    }
  }

  double straightPower = -leftJoystick_->GetY();
  double turnPower = rightJoystick_->GetX();
  DriverStationLCD::GetInstance()->PrintfLine(DriverStationLCD::kUser_Line5, "l:%7.3f r:%7.3f", straightPower, turnPower);
  oldIncreaseButton_ = operatorControl_->GetIncreaseButton();
  oldDecreaseButton_ = operatorControl_->GetDecreaseButton();
  oldAutonSelectButton_ = operatorControl_->GetAutonSelectButton();
  oldUnjamButton_ = operatorControl_->GetUnjamButton();

  // Show selected mode and delay on the Driver Station LCD.
  switch (autonMode_) {
    case AUTON_NONE:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line1, "No auton");
      break;
    case AUTON_START_ANYWHERE:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line1, "two anywhere");
      break;
    case AUTON_FENDER:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line1, "Fender");
      break;
    case AUTON_2_PLUS_2:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line1, "2, bridge, 2");
      break;
    case AUTON_1_PLUS_3:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line1, "1, bridge, 3");
      break;
    case AUTON_0_PLUS_2_PLUS_2:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line1, "0, br, 2, 2");
      break;
    case AUTON_SHOOT_FROM_BRIDGE:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line1, "Far shoot@bridge");
      break;
    case AUTON_SIDE:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line1, "Side Corner");
      break;
    case AUTON_SHORT_SIMPLE:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line1, "Short Simple");
      break;
    case AUTON_FAR_SIMPLE:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line1, "Far Simple");
      break;
    case AUTON_ALLIANCE_BRIDGE:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line1, "Alliance bridge");
      break;
    case AUTON_TEST:
        lcd_->PrintfLine(DriverStationLCD::kUser_Line1, "Testing Auton");
        break;
    default:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line1, "Invalid auton");
  }

  switch (autonBias_) {
    case BIAS_LEFT:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line6, "B: L D: %.1f", autonDelay_);
      break;
    case BIAS_RIGHT:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line6, "B: R D: %.1f", autonDelay_);
      break;
    default:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line6, "B: N D: %.1f", autonDelay_);
      break;
  }

    //Ball hardness selection
    if (operatorControl_->GetKeyFarButton() && !oldHardUpButton_) {
      ballHardness_ = (BallHardness)(ballHardness_ + 1);
      if (ballHardness_ == NUM_BALLS) {
        ballHardness_ = BALL_DEFAULT;
      }
    } else if (operatorControl_->GetKeyCloseButton() && !oldHardDownButton_) {
        ballHardness_ = (BallHardness)(ballHardness_ - 1);
        if (ballHardness_ == NUM_BALLS || ballHardness_ < 0) {
          ballHardness_ = BALL_DEFAULT;
        }
      }
  oldHardUpButton_ = operatorControl_->GetKeyFarButton();
  oldHardDownButton_ = operatorControl_->GetKeyCloseButton();
  switch(ballHardness_) {
    case BALL_WTF_SOFT:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line3, "REALLY Soft Balls");
      break;
    case BALL_SOFT:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line3, "Soft Balls");
      break;
    case  BALL_DEFAULT:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line3, "Default Ball Hardness");
      break;
    case BALL_HARD:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line3, "Hard Balls");
      break;
    case BALL_WTF_HARD:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line3, "REALLY Hard Balls");
      break;
    default:
      lcd_->PrintfLine(DriverStationLCD::kUser_Line3, "Yo Dawgs you broke something");
      break;
  }

  // Show any other pre-match stuff we're interested in on the Driver Station LCD.
  lcd_->PrintfLine(DriverStationLCD::kUser_Line4, "Gyro: %f", gyro_->GetAngle());
  static int i = 0;
  if (++i % 10 == 0)
    lcd_->UpdateLCD();

  SmartDashboard::GetInstance()->PutBoolean("Aligned", target_->SeesTarget() && target_->GetAngle() < .5 && target_->GetAngle() > -.5);

}

void Skyfire::AutonomousPeriodic() {
  GetWatchdog().Feed();
  if ((autonTimer_->Get() > autonDelay_)&& autoBaseCmd_) {
    autoBaseCmd_->Run();
  }
  autonSafetyTimer_->Reset();
}

void Skyfire::TeleopPeriodic() {
  GetWatchdog().Feed();
  static bool autoshooting = false;

  // Drive stuff up here so we can see if aligning done before shooting
  // Only have Teleop and AutoAlign Drivers right now
  if (leftJoystick_->GetRawButton((int)constants_->autoAlignPort) && !oldAutoAlignButton_) {
    // If the auto-align button is pressed, switch to the auto-align driver.
    currDriver_ = autoAlignDriver_;
    currDriver_->Reset();
  } else if (!leftJoystick_->GetRawButton((int)constants_->autoAlignPort) && oldAutoAlignButton_) {
    // If the auto-align button is released, switch back to the teleop driver.
    currDriver_ = teleopDriver_;
    currDriver_->Reset();
  }
  oldAutoAlignButton_ = leftJoystick_->GetRawButton((int)constants_->autoAlignPort);
  oldControlLoopsSwitch_ = operatorControl_->GetControlLoopsSwitch();
  // Calculate the outputs for the drivetrain given the inputs.
  currDriver_->UpdateDriver();

  // Update shooter power/manual control
  if (operatorControl_->GetKeyFarButton() && operatorControl_->GetIncreaseButton()) {
    shooterTargetVelocity_ = 75;
  } else if (operatorControl_->GetFenderButton()) {
    shooterIncr_ = 0.0;
    autoshooting = false;
    shooterTargetVelocity_ = constants_->shooterFenderSpeed;
  } else if (operatorControl_->GetFarFenderButton()) {
    autoshooting = false;
    shooterIncr_ = 0.0;
    shooterTargetVelocity_ = constants_->shooterFarFenderSpeed;
  } else if (operatorControl_->GetKeyCloseButton()) {
    shooterIncr_ = 0.0;
    autoshooting = false;
    shooterTargetVelocity_ = constants_->shooterKeyCloseSpeed;
  } else if (operatorControl_->GetKeyFarButton()) {
    shooterIncr_ = 0.0;
    autoshooting = false;
    shooterTargetVelocity_ = constants_->shooterKeyFarSpeed;
  } else if (operatorControl_->GetIncreaseButton() && !oldIncreaseButton_) {
    shooterIncr_ += constants_->shooterSpeedIncrement;
  } else if (operatorControl_->GetDecreaseButton() && !oldDecreaseButton_) {
    shooterIncr_ -= constants_->shooterSpeedIncrement;
  }

  Shooter::hoodPref pref = Shooter::NO;
  double dist = target_->GetDistance();
  double autoDistanceVel = (((constants_->shooterKeyFarSpeed - constants_->shooterKeyCloseSpeed ) / (190 - 122)) *
                (dist - 122)) + constants_->shooterKeyCloseSpeed;
  if(dist < 111) {
    autoDistanceVel = (((constants_->shooterFarFenderSpeed - constants_->shooterFenderSpeed ) / (95 - 60)) *
                        (dist - 60)) + constants_->shooterFenderSpeed;
  }

  if (operatorControl_->GetShootButton()) {
    // In manual shoot mode, run the conveyor and intake to feed the shooter.
    shooter_->SetLinearConveyorPower(1.0);
    intake_->SetIntakePower(1.0);
  } else if (operatorControl_->GetAutoShootButton()) {
    shooter_->SetLinearConveyorPower(1.0);
    intake_->SetIntakePower(0.0);
    reverseConveyor_ = 4;
  } else if (operatorControl_->GetAutonSelectButton()) {
    shooter_->SetLinearConveyorPower(-1.0);
    intake_->SetIntakePower(0.0);
  } else if (operatorControl_->GetUnjamButton()) {
    // In exhaust mode, run the conveyor and intake backwards.
    intake_->SetIntakePower(-1.0);
    shooter_->SetLinearConveyorPower(-1.0);
  } else if (operatorControl_->GetIntakeButton()) {
    // In intake mode, run the intake forwards and the conveyor backwards to jumble balls in the hopper.
    intake_->SetIntakePower(1.0);
    shooter_->SetLinearConveyorPower(-1.0);
  } else if (reverseConveyor_ > 0) {
    shooter_->SetLinearConveyorPower(-1.0);
    intake_->SetIntakePower(0.0);
  } else {
    shooter_->SetLinearConveyorPower(0.0);
    intake_->SetIntakePower(0.0);
  }
  reverseConveyor_--;
  if (reverseConveyor_ < 0)
    reverseConveyor_ = 0;

  if (operatorControl_->GetShooterSwitch()) {
    // Re-load the shooter PID constants whenever the shooter is turned on.
    if (!oldShooterSwitch_) {
      if (shooterTargetVelocity_ == 0) {
        shooterTargetVelocity_ = constants_->shooterFenderSpeed;
      }
      constants_->LoadFile();
    }
    if (autoshooting && target_->SeesTarget()) {
      shooterTargetVelocity_ = autoDistanceVel;
    }
    shooter_->SetTargetVelocity(shooterTargetVelocity_ + shooterIncr_, pref);
  } else {
    shooter_->SetTargetVelocity(0);
  }
  // Update shooter button guards
  oldShooterSwitch_ = operatorControl_->GetShooterSwitch();
  oldIncreaseButton_ = operatorControl_->GetIncreaseButton();
  oldDecreaseButton_ = operatorControl_->GetDecreaseButton();

  if (!drivebase_->GetPizzaUp()) {
    // If pizza wheels are down, set the intake up to prevent damage.
    intake_->SetIntakePosition(Intake::INTAKE_UP);
  } else {
    intake_->SetIntakePosition(operatorControl_->GetIntakePositionSwitch());
  }

  if(operatorControl_->GetAutonSelectButton()) {
    if(operatorControl_->GetIncreaseButton()) {
      dingusSolenoid_->Set(true);
    } else if(operatorControl_->GetDecreaseButton()) {
      dingusSolenoid_->Set(false);
    }
  }

  // Print useful information to the LCD display.
  lcd_->PrintfLine(DriverStationLCD::kUser_Line3, "Le: %0.2f R: %0.2f", drivebase_->GetLeftEncoderDistance(), drivebase_->GetRightEncoderDistance());
  lcd_->PrintfLine(DriverStationLCD::kUser_Line4, "X: %0.2f A:%0.2f ", target_->GetX(), target_->GetAngle());
  lcd_->PrintfLine(DriverStationLCD::kUser_Line5, "%0.2f %0.2f", target_->GetDistance(), target_->GetHDiff());
  lcd_->PrintfLine(DriverStationLCD::kUser_Line5, "%f", target_->GetAngle());
  static int i = 0;
  if (++i % 10 == 0) {
    lcd_->UpdateLCD();
  }

  double curLeft = drivebase_->GetLeftEncoderDistance();
  double curRight = drivebase_->GetRightEncoderDistance();
  double curTime = timer_->Get();
  double dt = curTime - prevTime;
  prevTime = curTime;
  static const double maxSpeed = 10.0;
  // Set the brake in the last 0.25 seconds of the match
  if (timer_->Get() >= 119.75 && fabs(curLeft - prevLeftDist_) / dt < maxSpeed && fabs(curRight - prevRightDist_) / dt < maxSpeed) {
    //Dawg does this is set to false for demos doe, for competition make it true
    teleopDriver_->AskForBrake(false);
  } else {
    teleopDriver_->AskForBrake(false);
  }
  prevLeftDist_ = curLeft;
  prevRightDist_ = curRight;

  static int r = 0;
  r++;
  bool light = false;
  if (leftJoystick_->GetRawButton((int)constants_->autoAlignPort) && target_->SeesTarget() && target_->GetAngle() > -.85 && target_->GetAngle() < .85) {
    if (lightDelay_->Get() > .125) {
      light = ((r % 15) <= 8) ;
    }
  }
  else {
    lightDelay_->Reset();
  }
  SmartDashboard::GetInstance()->PutBoolean("Aligned", light );
}
