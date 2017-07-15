// motor controller (normal & perimeter tracking), odometry

#define IMU_DEBUG

/*
Hier rausgenommen -> siehe ODO-ISR in der mower.cpp

// ---- odometry (interrupt) --------------------------------------------------------
// Determines the rotation count and direction of the odometry encoders. Called in the odometry pins interrupt.
// encoder signal/Ardumower pinout etc. at http://wiki.ardumower.de/index.php?title=Odometry
// Logic is: 
//    If the pin1 change transition (odometryLeftState) is LOW -> HIGH... 
//      If the pin2 current state is HIGH :  step count forward   (odometryLeft++)
//        Otherwise :  step count reverse   (odometryLeft--)   
// odometryState:  1st left and right odometry signal
// odometryState2: 2nd left and right odometry signal (optional two-wire encoders)
void Robot::setOdometryState(unsigned long timeMicros, boolean odometryLeftState, boolean odometryRightState, boolean odometryLeftState2, boolean odometryRightState2){
  int leftStep = 1;
  int rightStep = 1;
  if (odometryLeftSwapDir) leftStep = -1;
  if (odometryRightSwapDir) rightStep = -1;
  if (odometryLeftState != odometryLeftLastState){    
    if (odometryLeftState){ // pin1 makes LOW->HIGH transition
      if (twoWayOdometrySensorUse) { 
        // pin2 = HIGH? => forward 
        if (odometryLeftState2) odometryLeft += leftStep; else odometryLeft -= leftStep;
      } 
      else { 
         if (motorLeftPWMCurr >=0) odometryLeft ++; else odometryLeft --;
      }
    }
    odometryLeftLastState = odometryLeftState;
  } 

  if (odometryRightState != odometryRightLastState){
    if (odometryRightState){ // pin1 makes LOW->HIGH transition
      if (twoWayOdometrySensorUse) {
        // pin2 = HIGH? => forward
        if (odometryRightState2) odometryRight += rightStep; else odometryRight -= rightStep;
      }     
      else {
         if (motorRightPWMCurr >=0) odometryRight ++; else odometryRight --;    
      }
    }
    odometryRightLastState = odometryRightState;
  }  
  if (twoWayOdometrySensorUse) {
    if (odometryRightState2 != odometryRightLastState2){
      odometryRightLastState2 = odometryRightState2;    
    }  
    if (odometryLeftState2 != odometryLeftLastState2){
      odometryLeftLastState2 = odometryLeftState2;    
    }
  }    
}
*/


// ---- motor RPM (interrupt) --------------------------------------------------------------
// mower motor RPM driver
void Robot::setMotorMowRPMState(boolean motorMowRpmState){
  if (motorMowRpmState != motorMowRpmLastState) {    
    motorMowRpmLastState = motorMowRpmState;
    if (motorMowRpmLastState) motorMowRpmCounter++;   
  }
}



// calculate map position by odometry sensors
void Robot::calcOdometry(){
  if ((!odometryUse) || (millis() < nextTimeOdometry)) return;    
    nextTimeOdometry = millis() + 300;

  static int lastOdoLeft = 0;
  static int lastOdoRight = 0;
  int odoLeft = odometryLeft;
  int odoRight = odometryRight;
  int ticksLeft = odoLeft - lastOdoLeft;
  int ticksRight = odoRight - lastOdoRight;
  lastOdoLeft = odoLeft;
  lastOdoRight = odoRight;    
  double left_cm = ((double)ticksLeft) / ((double)odometryTicksPerCm);
  double right_cm = ((double)ticksRight) / ((double)odometryTicksPerCm);  
  double avg_cm  = (left_cm + right_cm) / 2.0;
  double wheel_theta = (left_cm - right_cm) / ((double)odometryWheelBaseCm);
  odometryTheta += wheel_theta; 
  
  motorLeftRpmCurr  = double ((( ((double)ticksLeft) / ((double)odometryTicksPerRevolution)) / ((double)(millis() - lastMotorRpmTime))) * 60000.0); 
  motorRightRpmCurr = double ((( ((double)ticksRight) / ((double)odometryTicksPerRevolution)) / ((double)(millis() - lastMotorRpmTime))) * 60000.0);                
  lastMotorRpmTime = millis();
               
  if (imuUse){
    odometryX += avg_cm * sin(imu.ypr.yaw); 
    odometryY += avg_cm * cos(imu.ypr.yaw); 
  } else {
    // FIXME: theta should be old theta, not new theta?
    odometryX += avg_cm * sin(odometryTheta); 
    odometryY += avg_cm * cos(odometryTheta); 
 }
}


// sets wheel motor actuators
// - driver protection: delays polarity change until motor speed (EMV) is zero
//   http://wiki.ardumower.de/images/a/a5/Motor_polarity_switch_protection.png 
// - optional: ensures that the motors (and gears) are not switched to 0% (or 100%) too fast (motorAccel)
void Robot::setMotorPWM(int pwmLeft, int pwmRight, boolean useAccel){
  unsigned long TaC = millis() - lastSetMotorSpeedTime;    // sampling time in millis
  lastSetMotorSpeedTime = millis();  
  if (TaC > 1000) TaC = 1;

  if (useAccel){  
    // http://phrogz.net/js/framerate-independent-low-pass-filter.html 
    // smoothed += elapsedTime * ( newValue - smoothed ) / smoothing;          
    motorLeftPWMCurr += TaC * (pwmLeft - motorLeftPWMCurr) / motorAccel;
    motorRightPWMCurr += TaC * (pwmRight - motorRightPWMCurr) / motorAccel;   
  }

  // ----- driver protection (avoids driver explosion) ----------
  if ( ((pwmLeft < 0) && (motorLeftPWMCurr >= 0)) ||
       ((pwmLeft > 0) && (motorLeftPWMCurr <= 0)) ) { // changing direction should take place?
    if (motorLeftZeroTimeout != 0)
      pwmLeft = motorLeftPWMCurr - motorLeftPWMCurr *  ((float)TaC)/200.0; // reduce speed
  }
  if ( ((pwmRight < 0) && (motorRightPWMCurr >= 0)) ||
       ((pwmRight > 0) && (motorRightPWMCurr <= 0)) ) { // changing direction should take place?    
    if (motorRightZeroTimeout != 0) // reduce motor rotation? (will reduce EMF)      
      pwmRight = motorRightPWMCurr - motorRightPWMCurr *   ((float)TaC)/200.0;  // reduce speed
  }

  motorLeftPWMCurr = pwmLeft;
  motorRightPWMCurr = pwmRight;

  if (odometryUse){
    if (abs(motorLeftRpmCurr) <1) motorLeftZeroTimeout = max(0, ((int)(motorLeftZeroTimeout - TaC)) );
      else motorLeftZeroTimeout = 500;
    if (abs(motorRightRpmCurr) <1) motorRightZeroTimeout = max(0, ((int)(motorRightZeroTimeout - TaC)) );      
      else motorRightZeroTimeout = 500;
  } else {
    if (pwmLeft == 0)  motorLeftZeroTimeout = max(0, ((int)(motorLeftZeroTimeout - TaC)) );
      else motorLeftZeroTimeout = 700;  
    if (pwmRight == 0) motorRightZeroTimeout = max(0, ((int)(motorRightZeroTimeout - TaC)) );      
      else motorRightZeroTimeout = 700;  
  }

  // ---------------------------------
  if (motorLeftSwapDir)  // swap pin polarity?
    setActuator(ACT_MOTOR_LEFT, -motorLeftPWMCurr);
  else
    setActuator(ACT_MOTOR_LEFT, motorLeftPWMCurr);
  if (motorRightSwapDir)   // swap pin polarity?
    setActuator(ACT_MOTOR_RIGHT, -motorRightPWMCurr);
  else 
    setActuator(ACT_MOTOR_RIGHT, motorRightPWMCurr);
}


// sets mower motor actuator
// - ensures that the motor is not switched to 100% too fast (motorMowAccel)
// - ensures that the motor voltage is not higher than motorMowSpeedMaxPwm
void Robot::setMotorMowPWM(int pwm, boolean useAccel){
  unsigned long TaC = millis() - lastSetMotorMowSpeedTime;    // sampling time in millis
  lastSetMotorMowSpeedTime = millis();  
  if (TaC > 1000) TaC = 1;  
  // we need to ignore acceleration for PID control, and we can ignore if speed is lowered (e.g. motor is shut down)
  if ( (!useAccel) || (pwm < motorMowPWMCurr) )
    motorMowPWMCurr = pwm;
  else {
    // http://phrogz.net/js/framerate-independent-low-pass-filter.html
    // smoothed += elapsedTime * ( newValue - smoothed ) / smoothing;          
    motorMowPWMCurr += TaC * (pwm - motorMowPWMCurr) / motorMowAccel;
  }
  setActuator(ACT_MOTOR_MOW, min(motorMowSpeedMaxPwm, max(0, motorMowPWMCurr)));
}

// PID controller: roll robot to heading (requires IMU)
void Robot::motorControlImuRoll(){
  if (millis() < nextTimeMotorImuControl) return;
  nextTimeMotorImuControl = millis() + 100;

  
  // Regelbereich entspricht 80% der maximalen Drehzahl am Antriebsrad (motorSpeedMaxRpm)
  imuRollPID.x = distancePI(imu.ypr.yaw, imuRollHeading) / PI * 180.0;            
  imuRollPID.w = 0;
  imuRollPID.y_min = -motorSpeedMaxPwm/1.25;        // da der Roll generell langsamer erfolgen soll
  imuRollPID.y_max = motorSpeedMaxPwm/1.25;   // 
  imuRollPID.max_output = motorSpeedMaxRpm/1.25;    //
  imuRollPID.compute();                 

  // Regelbereich entspricht maximaler PWM am Antriebsrad (motorSpeedMaxPwm), um auch an Steigungen h�chstes Drehmoment f�r die Solldrehzahl zu gew�hrleisten
  motorLeftPID.x = motorLeftRpmCurr;                 // IST 
  motorLeftPID.w = -imuRollPID.y;                // SOLL 
  motorLeftPID.y_min = -motorSpeedMaxPwm;        // Regel-MIN
  motorLeftPID.y_max = motorSpeedMaxPwm;   // Regel-MAX
  motorLeftPID.max_output = motorSpeedMaxPwm;    // Begrenzung
  motorLeftPID.compute();
  //int leftSpeed = max(-motorSpeedMaxPwm, min(motorSpeedMaxPwm, motorLeftPWMCurr + motorLeftPID.y));
  //if((motorLeftSpeedRpmSet >= 0 ) && (leftSpeed <0 )) leftSpeed = 0;
  //if((motorLeftSpeedRpmSet <= 0 ) && (leftSpeed >0 )) leftSpeed = 0;     
  // int leftSpeed = max(-motorSpeedMaxPwm, min(motorSpeedMaxPwm, motorLeftPID.y));
  //int leftSpeed = max(-motorSpeedMaxPwm, min(motorSpeedMaxPwm, motorLeftPID.y));
  int leftSpeed = max(-motorSpeedMaxPwm, min(motorSpeedMaxPwm, -imuRollPID.y));

  // Regelbereich entspricht maximaler PWM am Antriebsrad (motorSpeedMaxPwm), um auch an Steigungen h�chstes Drehmoment f�r die Solldrehzahl zu gew�hrleisten
  motorRightPID.Kp = motorLeftPID.Kp;
  motorRightPID.Ki = motorLeftPID.Ki;
  motorRightPID.Kd = motorLeftPID.Kd;
  motorRightPID.x = motorRightRpmCurr;               // IST   
  motorRightPID.w = imuRollPID.y;                // SOLL
  motorRightPID.y_min = -motorSpeedMaxPwm;       // Regel-MIN
  motorRightPID.y_max = motorSpeedMaxPwm;  // Regel-MAX 
  motorRightPID.max_output = motorSpeedMaxPwm;   // Begrenzung
  motorRightPID.compute();            
  //int rightSpeed = max(-motorSpeedMaxPwm, min(motorSpeedMaxPwm, motorRightPWMCurr + motorRightPID.y));
  //int rightSpeed = max(-motorSpeedMaxPwm, min(motorSpeedMaxPwm, motorRightPID.y));
  //int rightSpeed = max(-motorSpeedMaxPwm, min(motorSpeedMaxPwm, motorRightPID.y));
  int rightSpeed = max(-motorSpeedMaxPwm, min(motorSpeedMaxPwm, imuRollPID.y));
  //if((motorRightSpeedRpmSet >= 0 ) && (rightSpeed <0 )) rightSpeed = 0;
  //if((motorRightSpeedRpmSet <= 0 ) && (rightSpeed >0 )) rightSpeed = 0;         

  #ifdef IMU_DEBUG
  Console.print("roll yaw: ");
  Console.print(imu.ypr.yaw, 4);
  Console.print(',');
  Console.print(imuRollHeading, 4);
  Console.print(" pid: ");
  Console.print(imuRollPID.x, 4);
  Console.print(',');
  Console.print(imuRollPID.y, 4);
  Console.print(" left: ");
  Console.print(motorLeftPWMCurr, 4);
  Console.print(',');
  Console.print(motorLeftPID.y, 4);
  Console.print(',');
  Console.print(leftSpeed);
  Console.print(" right: ");
  Console.print(motorRightPWMCurr, 4);
  Console.print(',');
  Console.print(motorRightPID.y, 4);
  Console.print(',');
  Console.println(rightSpeed);
  #endif
  
  if (  ((stateCurr == STATE_OFF) || (stateCurr == STATE_STATION) || (stateCurr == STATE_ERROR)) && (millis()-stateStartTime>1000)  ){
    leftSpeed = rightSpeed = 0; // ensures PWM is zero if OFF/CHARGING
  }
  setMotorPWM( leftSpeed, rightSpeed, false );                   

}



// PID controller: track perimeter 
void Robot::motorControlPerimeter(){    
  if (millis() < nextTimeMotorPerimeterControl) return;
    nextTimeMotorPerimeterControl = millis() + 100;

  if ((millis() > stateStartTime + 5000) && (millis() > perimeterLastTransitionTime + trackingPerimeterTransitionTimeOut)){
    // robot is wheel-spinning while tracking => roll to get ground again
    if (trackingBlockInnerWheelWhilePerimeterStruggling == 0){
    if (perimeterInside) setMotorPWM( -motorSpeedMaxPwm/1.5, motorSpeedMaxPwm/1.5, false);
        else setMotorPWM( motorSpeedMaxPwm/1.5, -motorSpeedMaxPwm/1.5, false);}

    else if (trackingBlockInnerWheelWhilePerimeterStruggling == 1){
      if (perimeterInside) setMotorPWM( 0, motorSpeedMaxPwm/1.5, false);
        else setMotorPWM( motorSpeedMaxPwm/1.5, 0, false);
    }

    if (millis() > perimeterLastTransitionTime + trackingErrorTimeOut){      
      Console.println("Error: tracking error");
      addErrorCounter(ERR_TRACKING);
      //setNextState(STATE_ERROR,0);
      setNextState(STATE_PERI_FIND,0);
    }
    return;
  }   
  if (perimeterInside)
      perimeterPID.x = -1;
    else
      perimeterPID.x = 1;

  perimeterPID.w = 0;
  perimeterPID.y_min = -motorSpeedMaxPwm;
  perimeterPID.y_max = motorSpeedMaxPwm;		
  perimeterPID.max_output = motorSpeedMaxPwm;
  perimeterPID.compute();
  //setMotorPWM( motorLeftPWMCurr  +perimeterPID.y, 
  //               motorRightPWMCurr -perimeterPID.y, false);      
  setMotorPWM( max(-motorSpeedMaxPwm, min(motorSpeedMaxPwm, motorSpeedMaxPwm/2 - perimeterPID.y)), 
                 max(-motorSpeedMaxPwm, min(motorSpeedMaxPwm, motorSpeedMaxPwm/2 + perimeterPID.y)), false);      
  /*Console.print(perimeterPID.x);
  Console.print("\t");          
  Console.println(perimeterPID.y);  */
}

// PID controller: correct direction during normal driving (requires IMU)
void Robot::motorControlImuDir(){
  if (millis() < nextTimeMotorImuControl) return;
    nextTimeMotorImuControl = millis() + 100;

  int correctLeft = 0;
  int correctRight = 0;
  
  // Regelbereich entspricht maximaler Drehzahl am Antriebsrad (motorSpeedMaxRpm)
  imuDirPID.x = distancePI(imu.ypr.yaw, imuDriveHeading) / PI * 180.0;            
  imuDirPID.w = 0;
  imuDirPID.y_min = -motorSpeedMaxPwm;
  imuDirPID.y_max = motorSpeedMaxPwm;    
  imuDirPID.max_output = motorSpeedMaxRpm;
  imuDirPID.compute();                    
                 
  if (imuDirPID.y < 0) correctRight = abs(imuDirPID.y);
  if (imuDirPID.y > 0) correctLeft  = abs(imuDirPID.y);
                 
  // Korrektur erfolgt �ber Abbremsen des linken Antriebsrades, falls Kursabweichung nach rechts
  // Regelbereich entspricht maximaler PWM am Antriebsrad (motorSpeedMaxPwm), um auch an Steigungen h�chstes Drehmoment f�r die Solldrehzahl zu gew�hrleisten
  motorLeftPID.x = motorLeftRpmCurr;                     // IST 
  motorLeftPID.w = motorLeftSpeedRpmSet - correctLeft;     // SOLL 
  motorLeftPID.y_min = -motorSpeedMaxPwm;            // Regel-MIN
  motorLeftPID.y_max = motorSpeedMaxPwm;       // Regel-MAX
  motorLeftPID.max_output = motorSpeedMaxPwm;        // Begrenzung
  motorLeftPID.compute();
  // int leftSpeed = max(-motorSpeedMaxPwm, min(motorSpeedMaxPwm, motorLeftPWMCurr + motorLeftPID.y));
  int leftSpeed = max(-motorSpeedMaxPwm, min(motorSpeedMaxPwm, motorSpeedMaxPwm - correctLeft));
  if((motorLeftSpeedRpmSet >= 0 ) && (leftSpeed <0 )) leftSpeed = 0;
  if((motorLeftSpeedRpmSet <= 0 ) && (leftSpeed >0 )) leftSpeed = 0;    

  // Korrektur erfolgt �ber Abbremsen des rechten Antriebsrades, falls Kursabweichung nach links 
  // Regelbereich entspricht maximaler PWM am Antriebsrad (motorSpeedMaxPwm), um auch an Steigungen h�chstes Drehmoment f�r die Solldrehzahl zu gew�hrleisten
  motorRightPID.Kp = motorLeftPID.Kp;
  motorRightPID.Ki = motorLeftPID.Ki;
  motorRightPID.Kd = motorLeftPID.Kd;
  motorRightPID.x = motorRightRpmCurr;                   // IST 
  motorRightPID.w = motorRightSpeedRpmSet - correctRight;  // SOLL 
  motorRightPID.y_min = -motorSpeedMaxPwm;           // Regel-MIN
  motorRightPID.y_max = motorSpeedMaxPwm;      // Regel-MAX
  motorRightPID.max_output = motorSpeedMaxPwm;       // Begrenzung
  motorRightPID.compute();            
  // int rightSpeed = max(-motorSpeedMaxPwm, min(motorSpeedMaxPwm, motorRightPWMCurr + motorRightPID.y));
  int rightSpeed = max(-motorSpeedMaxPwm, min(motorSpeedMaxPwm, motorSpeedMaxPwm - correctRight));
  if((motorRightSpeedRpmSet >= 0 ) && (rightSpeed <0 )) rightSpeed = 0;
  if((motorRightSpeedRpmSet <= 0 ) && (rightSpeed >0 )) rightSpeed = 0;         
  
  if (  ((stateCurr == STATE_OFF) || (stateCurr == STATE_STATION) || (stateCurr == STATE_ERROR)) && (millis()-stateStartTime>1000)  ){
    leftSpeed = rightSpeed = 0; // ensures PWM is zero if OFF/CHARGING
  }

  #ifdef IMU_DEBUG
  Console.print("dir yaw: ");
  Console.print(imu.ypr.yaw, 4);
  Console.print(',');
  Console.print(imuDriveHeading, 4);
  Console.print(" pid: ");
  Console.print(imuDirPID.x, 4);
  Console.print(',');
  Console.print(imuDirPID.y, 4);
  Console.print(" left: ");
  Console.print(motorLeftPWMCurr, 4);
  Console.print(',');
  Console.print(motorLeftPID.y, 4);
  Console.print(',');
  Console.print(leftSpeed);
  Console.print(" right: ");
  Console.print(motorRightPWMCurr, 4);
  Console.print(',');
  Console.print(motorRightPID.y, 4);
  Console.print(',');
  Console.println(rightSpeed);
  #endif
  setMotorPWM( leftSpeed, rightSpeed, false );                   
                 
}

// check for odometry sensor faults    
void Robot::checkOdometryFaults(){
  if (!odometryUse)  return;
  bool leftErr = false;
  bool rightErr = false;
  if ((stateCurr == STATE_FORWARD) &&  (millis()-stateStartTime>4000) ) {
    // just check if odometry sensors may not be working at all
    if ( (motorLeftPWMCurr > 100) && (abs(motorLeftRpmCurr) < 1)  )  leftErr = true;
    if ( (motorRightPWMCurr > 100) && (abs(motorRightRpmCurr) < 1)  ) rightErr = true;
  }  
  if ((stateCurr == STATE_ROLL) &&  (millis()-stateStartTime>3000) ) {
    // just check if odometry sensors may be turning in the wrong direction
    if ( ((motorLeftPWMCurr > 100) && (motorLeftRpmCurr < -3)) || ((motorLeftPWMCurr < -100) && (motorLeftRpmCurr > 3)) ) leftErr = true;
    if ( ((motorRightPWMCurr > 100) && (motorRightRpmCurr < -3)) || ((motorRightPWMCurr < -100) && (motorRightRpmCurr > 3)) ) rightErr = true;
  }  
  if (leftErr){
    Console.print("Left odometry error: PWM=");
    Console.print(motorLeftPWMCurr);
    Console.print("\tRPM=");
    Console.println(motorLeftRpmCurr);
    addErrorCounter(ERR_ODOMETRY_LEFT);
    // setNextState(STATE_ERROR, 0);
    resetMotorFault();
    delay(200);
  }
  if (rightErr){
    Console.print("Right odometry error: PWM=");
    Console.print(motorRightPWMCurr);
    Console.print("\tRPM=");
    Console.println(motorRightRpmCurr);
    addErrorCounter(ERR_ODOMETRY_RIGHT);
    // setNextState(STATE_ERROR, 0);
    resetMotorFault();
    delay(200);
  }
}


void Robot::motorControl(){
  if (millis() < nextTimeMotorControl) return;
    nextTimeMotorControl = millis() + 100;
    static unsigned long nextMotorControlOutputTime = 0;
  if (odometryUse){
    // Regelbereich entspricht maximaler PWM am Antriebsrad (motorSpeedMaxPwm), um auch an Steigungen h�chstes Drehmoment f�r die Solldrehzahl zu gew�hrleisten
    motorLeftPID.w = motorLeftSpeedRpmSet;               // SOLL 
    motorRightPID.w = motorRightSpeedRpmSet;             // SOLL    
    float RLdiff = motorLeftRpmCurr - motorRightRpmCurr;
    if (motorLeftSpeedRpmSet == motorRightSpeedRpmSet){
      // line motion
      motorLeftPID.w = motorLeftSpeedRpmSet - RLdiff/2;
      motorRightPID.w = motorRightSpeedRpmSet + RLdiff/2;      
    }
    motorLeftPID.x = motorLeftRpmCurr;                 // IST     
    if (millis() < stateStartTime + motorZeroSettleTime) motorLeftPID.w = 0; // get zero speed first after state change
    motorLeftPID.y_min = -motorSpeedMaxPwm;        // Regel-MIN
    motorLeftPID.y_max = motorSpeedMaxPwm;     // Regel-MAX
    motorLeftPID.max_output = motorSpeedMaxPwm;    // Begrenzung
    motorLeftPID.compute();
    int leftSpeed = max(-motorSpeedMaxPwm, min(motorSpeedMaxPwm, motorLeftPWMCurr + motorLeftPID.y));
    // Sven reported the next two lines do make the PID controlling worse
    //if((motorLeftSpeedRpmSet >= 0 ) && (leftSpeed <0 )) leftSpeed = 0;
    //if((motorLeftSpeedRpmSet <= 0 ) && (leftSpeed >0 )) leftSpeed = 0;     

    // Regelbereich entspricht maximaler PWM am Antriebsrad (motorSpeedMaxPwm), um auch an Steigungen h�chstes Drehmoment f�r die Solldrehzahl zu gew�hrleisten
    motorRightPID.Kp = motorLeftPID.Kp;
    motorRightPID.Ki = motorLeftPID.Ki;
    motorRightPID.Kd = motorLeftPID.Kd;          
    motorRightPID.x = motorRightRpmCurr;               // IST
    if (millis() < stateStartTime + motorZeroSettleTime) motorRightPID.w = 0; // get zero speed first after state change
    motorRightPID.y_min = -motorSpeedMaxPwm;       // Regel-MIN
    motorRightPID.y_max = motorSpeedMaxPwm;        // Regel-MAX
    motorRightPID.max_output = motorSpeedMaxPwm;   // Begrenzung
    motorRightPID.compute();            
    int rightSpeed = max(-motorSpeedMaxPwm, min(motorSpeedMaxPwm, motorRightPWMCurr + motorRightPID.y));
    // Sven reported the next two lines do make the PID controlling worse
    //if((motorRightSpeedRpmSet >= 0 ) && (rightSpeed <0 )) rightSpeed = 0;
    //if((motorRightSpeedRpmSet <= 0 ) && (rightSpeed >0 )) rightSpeed = 0;         

    if ( (abs(motorLeftPID.x) < 2) && (abs(motorLeftPID.w) < 0.1) ) leftSpeed = 0; // ensures PWM is really zero 
    if ( (abs(motorRightPID.x)  < 2) && (abs(motorRightPID.w) < 0.1) ) rightSpeed = 0; // ensures PWM is really zero     

    /*if (millis() >= nextMotorControlOutputTime){
      nextMotorControlOutputTime = millis() + 3000; 
      Console.print("PID x=");
      Console.print(motorLeftPID.x);
      Console.print("\tPID w=");
      Console.print(motorLeftPID.w);
      Console.print("\tPID y=");
      Console.print(motorLeftPID.y);
      Console.print("\tPWM=");
      Console.println(leftSpeed);            
    } */ 

    setMotorPWM( leftSpeed, rightSpeed, false );              
  }
  else{
    int leftSpeed = min(motorSpeedMaxPwm, max(-motorSpeedMaxPwm, map(motorLeftSpeedRpmSet, -motorSpeedMaxRpm, motorSpeedMaxRpm, -motorSpeedMaxPwm, motorSpeedMaxPwm)));
    int rightSpeed =min(motorSpeedMaxPwm, max(-motorSpeedMaxPwm, map(motorRightSpeedRpmSet, -motorSpeedMaxRpm, motorSpeedMaxRpm, -motorSpeedMaxPwm, motorSpeedMaxPwm)));
    if (millis() < stateStartTime + motorZeroSettleTime) {
      leftSpeed = rightSpeed = 0; // slow down at state start      
      if (mowPatternCurr != MOW_LANES) imuDriveHeading = imu.ypr.yaw; // set drive heading    
    }
    setMotorPWM( leftSpeed, rightSpeed, true ); 
  }  
}


// motor mow speed controller (slowly adjusts output speed to given input speed)
// input: motorMowEnable, motorMowModulate, motorMowRpmCurr
// output: motorMowPWMCurr
void Robot::motorMowControl(){
  if (millis() < nextTimeMotorMowControl) return;

    nextTimeMotorMowControl = millis() + 100;
    if (motorMowEnableOverride) motorMowEnable = false;
  double mowSpeed ;
  if (!motorMowEnable) {
    mowSpeed = 0;         
    lastMowSpeedPWM = mowSpeed;
    motorMowPID.esum=0; 
    motorMowPID.x = 0;    
    setMotorMowPWM(mowSpeed, true);
  } 
  else {
    //if ((motorMowModulate) && (motorMowRpmCurr != 0)){
      // speed sensor available
    if (motorMowModulate){


      if (mowSpeed <motorMowRPMSet ){

        mowSpeed = lastMowSpeedPWM + 200;
        if (mowSpeed >motorMowRPMSet) mowSpeed = motorMowRPMSet;
      } else if (mowSpeed >motorMowRPMSet ){
        mowSpeed = lastMowSpeedPWM - 200;
        if (mowSpeed <motorMowRPMSet) mowSpeed = motorMowRPMSet;
      }

      motorMowPID.x = 0.2* motorMowRpmCurr + 0.8 * motorMowPID.x;      
      motorMowPID.w = mowSpeed; // 3300 => 2300
      motorMowPID.y_min = -motorMowSpeedMaxPwm/2;
      motorMowPID.y_max = motorMowSpeedMaxPwm/2;		
      motorMowPID.max_output = motorMowSpeedMaxPwm/2;
      motorMowPID.compute(); 

      setMotorMowPWM(mowSpeed / 20.0 + motorMowPID.y, false);
      lastMowSpeedPWM = mowSpeed;
    } 
    else {
      if((errorCounter[ERR_MOW_SENSE] == 0) && (errorCounter[ERR_STUCK] == 0)){
      // no speed sensor available      
      mowSpeed = motorMowSpeedPWMSet;
      setMotorMowPWM(mowSpeed, true);
      lastMowSpeedPWM = mowSpeed;
      }
    }
  }  
}



void Robot::printOdometry(){
  Console.print(F("ODO,"));
  Console.print(odometryX);
  Console.print(",");
  Console.println(odometryY);  
  Console.print(F("ODO,"));
  Console.print(odometryX);
  Console.print(",");
  Console.println(odometryY);  
}


