#include <Arduino.h>
#include <Preferences.h>

#define numberOfAtomizers 3
#define atomizerPins {4,5,2} //4,5,2
#define pwmF 108000
#define pwmResolution 4
#define pwmMaxDutyCycle 15
#define powerSwitch12V 12
// const int sensorIn = 27;      // pin where the OUT pin from sensor is connected on Arduino
// int mVperAmp = 185;           // this the 5A version of the ACS712 -use 100 for 20A Module and 66 for 30A Module
// int Watt = 0;
// double Voltage = 0;
// double VRMS = 0;
// double AmpsRMS = 0;
// float R1 = 6800.0;
// float R2 = 12000.0;
int atomizerAddress[numberOfAtomizers] = atomizerPins;

// Atomizer Timing
int atomizerOnTime = 100000;
int atomizerOffTime = 3000;


// Lights Timing
int lightSwitch = 26;
int lights = 1;
int onTime = 100000;   //12hrs
int offTime = 100000;//43200000;   //12hrs
int prev_time = 0;

void setup()
{
  // Debug console
  Serial.begin(9600); //9600
  // This delay gives the chance to wait for a Serial Monitor without blocking if none is found
  delay(100); 

  for(int i=0; i<numberOfAtomizers; i++){
    ledcSetup(i, pwmF, pwmResolution);
    ledcAttachPin(atomizerAddress[i], i);
  }

  pinMode(powerSwitch12V, OUTPUT); // Set the 12V Relay controll pin to output
  digitalWrite(powerSwitch12V, HIGH); // Set the 12V Relay controll pin to output
  pinMode(lightSwitch, OUTPUT); // Set the 12V Relay controll pin to output
  digitalWrite(lightSwitch, HIGH);
}

void loop(){
  
  //  Serial.println(analogRead(currentSensor));//Print the read current on Serial monitor
    if (millis()-prev_time > onTime && lights ==1){
      digitalWrite(lightSwitch, LOW); 
      lights = 0;
      prev_time = millis();
    }
    if (millis()-prev_time> offTime && lights == 0){
      digitalWrite(lightSwitch, HIGH);
      lights = 1;
      prev_time = millis();
    }
    for(int i = 0; i<numberOfAtomizers; i++){
      ledcWrite(atomizerAddress[i], 0);
    }
    delay(atomizerOffTime);

    for(int i = 0; i<numberOfAtomizers; i++){
        ledcWrite(atomizerAddress[i], 3 * pwmMaxDutyCycle / 5);    
    }
    delay(atomizerOnTime);
    //prev_time = millis();
   // Serial.println(prev_time);
}
