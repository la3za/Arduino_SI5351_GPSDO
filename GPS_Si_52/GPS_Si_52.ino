/*
A Time Base using a GPS controlled Si5351A Adafruit board to generate 10 MHz, 26 MHz or any other frequency up to 110 MHz.
Permission is granted to use, copy, modify, and distribute this software and documentation for non-commercial purposes. 
(F2DC 17 April 2017)

I was greatly helped by the works of Gene Marcus W3PM, Jason Milldrum NT7S-Dana H. Myers K6JQ, Igor Gonzales Martin and many others.
Thanks to all of them. 

==> This new version V.5.0 (May2020) can be used with the present version of the NT7S Si5351 library. I tested it with the 2.1.4 NT7S revision.
==> Version V.5.2 (June 2020) : The LCD shows now error E as "Rel.err=-70e-8"

SW modified by Erik Kaashoek to allow for longer measurement times enabling more accuracy.

Adaptations by LA3ZA, June 2021:
- I2C display
- set backlight, control it by rotary encoder
- display GPS baudrate
- always 10 MHz
- removed display of time from interrupt routine


*/

// include the library code:
//#include <LiquidCrystal.h>
#include <LiquidCrystal_I2C.h> // Install NewliquidCrystal_1.3.4.zip

#include <string.h>
#include <ctype.h>
#include <avr/interrupt.h>  
#include <avr/io.h>
#include <Wire.h>
#include <si5351.h>

Si5351 si5351;

#define LCD_pwm                  11 //   LA3ZA
int brightness = 64; // initial brightness
int brightStep = 10;


// Set up MCU pins
#define ppsPin                   2 // from GPS 
#define FreqAlarm                3 // LED Alarm XtalFreq
#define RS                       7 // LCD RS
#define E                        8 // LCD Enable
#define DB4                      9 // LCD DB4
#define DB5                     10 // LCD DB5
#define DB6                     11 // LCD DB6
#define DB7                     12 // LCD DB7
#define FreqSelect               A3// 4 // Choice between 10MHZ (or another frequency) and F=26 MHz

// Encoder:
#define encoderPinA              6 
#define encoderPinB              4
#define GPSSelect                3 

static const uint32_t GPSBaud =  9600; // QRPlabs GPS, LA3ZA

// initialize the library with the numbers of the interface pins
//LiquidCrystal lcd(RS, E, DB4, DB5, DB6, DB7);
LiquidCrystal_I2C lcd(0x3F, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  // Set the LCD I2C address

// Variables 
byte res,measMax,Epos;
byte CptInit=1;
char StartCommand2[7] = "$GPRMC",buffer[300] = "";
int IndiceCount=0,StartCount=0,counter=0,indices[13];
int validGPSflag = 1,Chlength;
int byteGPS=-1,second=0,minute=0,hour=0;
unsigned long mult=0;         // Count of overflows of 16 bit counter
int alarm = 0;
unsigned int tcount=0;        // counts the seconds in a measurement cycle 
int start = 1;                // Wait one second after first pps 
int duration = 2;             // Initial measurement duration
int new_duration = 2;         // Value of duration for next measurement cycle
int available = 0;            // Flag set to 1 after pulse counting finished
int stable_count = 0;         // Count of consecutive measurements without correction needed.

int32_t measdif,              // Measured difference in 1/100 Hz
  calfact =0;                 // Current correction factor in 1/100 Hz
int64_t target_freq0,         // The 2.5MHz pulse counted to check SI5351 frequency output
  caltargetfreq0,             // Target count of pulses
  meas_freq0;                 // Actual count of pulses
int64_t target_freq1;         // Additional output frequency

unsigned int encoderA, encoderB, encoderC = 1; // LA3ZA

/* Clock - interrupt routine for counting the CLK0 2.5 MHz signal
Called every second by GPS 1PPS on Arduino Nano D2
*/
void PPSinterrupt()
{
  tcount++;// Increment the seconds counter
  if (tcount == start) // Start counting the 2.5 MHz signal from Si5351A CLK0
  {
    TCCR1B = 7;    //Clock on rising edge of pin 5
    duration = new_duration;
  }
  if (tcount == start+duration)  //Stop the counter : the 40 second gate time is elapsed
  {     
    TCCR1B = 0;      //Turn off counter
    meas_freq0 = mult * 0x10000ULL + TCNT1;   //meas_freq0 is the number of pulses counted during duration PPS.
    caltargetfreq0=2500000*duration;          //Calculate the target count     
    TCNT1 = 0;       //Reset counter to zero
    mult = 0;
    tcount = 0;      //Reset the seconds counter
    available = 1;
  }
  if(validGPSflag == 1) //Start the UTC timekeeping process
  {
    second++;  
    if (second == 60)   //Set time using GPS NMEA data 
    {
      minute++ ;
      second=0 ;
    }
    if (minute == 60) 
    {
      hour++;
      minute=0 ;
    }

 // display time on LCD removed from interrupt routine, too slow?, screwed up the whole thing with I2C
//    if (hour == 24) hour=0;
//    lcd.setCursor(7,0); // LCD cursor on the right part of Line 0
//    if (hour < 10) lcd.print ("0");
//    lcd.print (hour);
//    lcd.print (":");
//    if (minute < 10) lcd.print ("0");
//    lcd.print (minute);
//    lcd.print (":");
//    if (second < 10) lcd.print ("0");
//    lcd.print (second);
//    lcd.print ("Z");  // UTC Time Indicator
  }
}

// Timer 1 overflow intrrupt vector.
ISR(TIMER1_OVF_vect) 
{
  mult++;        //Increment overflow multiplier
  TIFR1 = (1<<TOV1);  //Clear overlow flag by shifting left 
}

void setup()
{
  Wire.begin(1);    // I2C bus address = 1
  si5351.init(SI5351_CRYSTAL_LOAD_8PF,0,0); // I choose 8pF because 10pF is too large for crystal frequency 
//to be properly adjusted on my Adafruit board. May be different with other boards?

//Set up Timer1 as a frequency counter - input at pin 5
  TCCR1B = 0;     //Disable Timer during setup
  TCCR1A = 0;     //Reset
  TCNT1  = 0;     //Reset counter to zero
  TIFR1  = 1;     //Reset overflow
  TIMSK1 = 1;     //Turn on overflow flag

  pinMode(FreqAlarm, OUTPUT); // Alarm LED for weird meas_freq0
 // Set up the LCD's number of columns and rows 
  lcd.begin(16,2); 

  // GPS 1pps input
  pinMode(ppsPin, INPUT);

  lcd.display();           // initialize LCD
  lcd.setCursor(0,1);
  lcd.print(" F2DC V.5.2"); // display version number on the LCD
  delay(1000);
  
   // Set up IO switches
  pinMode(FreqSelect, INPUT);  // Initialize the frequency select pin
  digitalWrite(FreqSelect, HIGH); // internal pull-up enabled 

  // Set Arduino D2 for external interrupt input on the rising edge of GPS 1PPS
  attachInterrupt(0, PPSinterrupt, RISING);  

  //Serial.begin(9600);  // Define the GPS port speed
  Serial.begin(GPSBaud);  // Define the GPS port speed, LA3ZA

//Read the frequency setting pin
  res=digitalRead(FreqSelect);
// When testing with the frequency beat method, add or substract 800 Hz (or your choice). 
  if (res==HIGH)  
    {
//   target_freq1 = 7000000000ULL; // Freq_1=100MHz
    target_freq1 = 1000000000ULL; //   Freq_1=10 MHz in 1/100th Hz // no choice, LA3ZA
    }
    else
    {
    target_freq1 = 1000000000ULL; //   Freq_1=10 MHz in 1/100th Hz
    }
   
  lcd.setCursor(0,1);
  // lcd.print("Waiting for GPS");
  lcd.print("GPS ");lcd.print(GPSBaud);lcd.print(" ...    "); // LA3ZA


  TCCR1B = 0;    //Turn off Counter

 // Turn OFF CLK2 
  si5351.output_enable(SI5351_CLK2,0); 

// Set up parameters
  calfact = -2800; // Determined experimentally for my Si5351A 25 MHz crystal. Run the 'si5351_calibration.ino' program 
  // proposed by NT7S in his examples software package to get the calfact value associated with your Si5351A card.
  // Then use this value as 'your calfact and your calfact_old' instead of -2800.

  target_freq0=250000000ULL;    // In 1/100 Hz   
  caltargetfreq0=2500000*duration;

// Set up Si5351 calibration factor and frequencies
  si5351.set_correction(calfact, SI5351_PLL_INPUT_XO); 
  si5351.drive_strength(SI5351_CLK0,SI5351_DRIVE_8MA); // define CLK0 output current
  si5351.set_freq(target_freq0, SI5351_CLK0);
  si5351.drive_strength(SI5351_CLK1,SI5351_DRIVE_8MA); // define CLK1 output current
  si5351.set_freq(target_freq1, SI5351_CLK1); 


  dispfreq1();
  
}
//******************************************************************
// Loop 
void loop()
{
  int lock = 0; 
  if (validGPSflag == 0) GPSprocess( ); //If GPS is selected, wait for valid NMEA data
  else
  {
    if(available) // Frequency calculation data available                                   
    {
       available = 0;              
// Compute calfactor (and update if needed)
        measdif =(int32_t)((meas_freq0 - 2 -caltargetfreq0)* 400 / duration); // Error E calculation           
        if(measdif<-50000 || measdif>+50000) // Impossible error, alarm
        {
          digitalWrite(FreqAlarm,LOW);   // meas_freq0 OK : turn the LED OFF 
          alarm = 1;
          LCDmeasdif(false); // display E (measdif) on the LCD
          new_duration = 1;
          stable_count = 0;
        }
        else  
        {
          digitalWrite(FreqAlarm,LOW);   // meas_freq0 OK : turn the LED OFF 
          alarm = 0;
          measMax = 600  / duration;    // More than 1 tick difference
          if(measdif == 0) // measdif>-measMax && measdif<+measMax) // On target
          {
            LCDmeasdif(true); // display E (measdif) on the LCD
            lock = 1;
            stable_count++;
//            if (stable_count >= 2) 
            {                    // Increase resolution
              stable_count=0;
              new_duration = duration * 2;
              if (new_duration > 400)
                new_duration = 400;
            }
          }
          else
          {
            stable_count = 0;
#if 1
            if (measdif > 0)
              measdif += measMax/2;
            else if (measdif < 0)  
              measdif -= measMax/2;
#endif
            calfact=measdif+calfact; // compute the new calfact
            LCDmeasdif(false); // Call the display Error E (measdif) routine
            if(measdif<-measMax*10 && measdif>+measMax*10) // Too large, increase speed
            {
              new_duration = duration / 2;
              if (new_duration == 0)
                new_duration = 1;
            }
          }      
          si5351.set_correction(calfact, SI5351_PLL_INPUT_XO);
          si5351.set_freq(target_freq0,SI5351_CLK0);
          si5351.set_freq(target_freq1,SI5351_CLK1);
        }
        Serial.print(hour);
        Serial.print(":");
        Serial.print(minute);
        Serial.print(":");
        Serial.print(second);
        Serial.print(" Z");
        
        Serial.print(" mdif=");
        Serial.print(measdif);

        Serial.print(" Mfr0=");
        Serial.print((double)meas_freq0);
        Serial.print(" Tfr0=");
        Serial.print((double)caltargetfreq0);
        Serial.print(" calf=");
        Serial.print(calfact); 
        Serial.print(" dur=");
        Serial.print(duration); 
        if (lock) 
          Serial.println(" Lock");
        else
          Serial.println(" ");
          
      }
  } 
  // LA3ZA --- Rotary encoder algorithm begins here
 
    byte encoderA = digitalRead(encoderPinA);
    byte encoderB = digitalRead(encoderPinB);
    if ((encoderA == HIGH) && (encoderC == LOW))
    {
      if (encoderB == LOW)
      {
        // Decrease brightnress
            brightness -= brightStep;
      }
      else
      {
        // Increase brightness
            brightness += brightStep;
      }
      
    }
    encoderC = encoderA; 

    
   brightness = max(brightness, 0);
   brightness = min(brightness, 255); 
        
   analogWrite(LCD_pwm, brightness);
  
}

//***********************************
// Display the Si5351A CLK1 frequency on the LCD
void dispfreq1()
{
  
  // Display frequency on the LCD
  int Freq_Display=target_freq1/10000000;
  float Freq_Disp_Fl=float(Freq_Display);
  Freq_Disp_Fl=Freq_Disp_Fl/10;
  lcd.setCursor(0,0);
  lcd.print(Freq_Disp_Fl,1);
  lcd.print("M   ");    
}

//************************************
// Display on the LCD the difference E between measured CLK0 and theoretical 100e6 
void LCDmeasdif(int good)
{
          lcd.setCursor(0,1);
          lcd.print("                ");
          lcd.setCursor(0,1);
          if (alarm)
            lcd.print("> "); 
          if (measdif == 0)
            lcd.print("lock");
          else
            lcd.print(measdif);
            if (duration > 99)   // LA3ZA
              lcd.setCursor(5,1); //LA3ZA
            else
              lcd.setCursor(6,1); 
          lcd.print(" "); 
          lcd.print(duration); 
          lcd.print("s "); 
          lcd.print(calfact);
}          

//***************************************
//  GPS NMEA processing starts here

void GPSprocess(void)
{
  byte temp,i;
  byteGPS=Serial.read();      // Read a byte coming from the GPS serial port 
  if (byteGPS == -1) // See if the port is empty yet
  { 
    delay(100);  
  } 
  else // NMEA string data begins here
  { 
    buffer[counter]=byteGPS;     // If there is serial port data, it is put in the buffer
    counter++;                  
    if (byteGPS==13) // If the received byte is = to 13, end of transmission
    {      
        IndiceCount=0;
        StartCount=0;
        
//      NMEA $GPRMC data begins here        
        for (int i=1;i<7;i++) // Check if the received command starts with $GPRMC
          { 
          if (buffer[i]==StartCommand2[i-1])
            {
            StartCount++;
            }
          }                

        if(StartCount==6) // If yes, continue and process the GPS data
          {
          for (int i=0;i<300;i++)
            {
            if (buffer[i]==',') // check for the position of the  "," separator
              {
              indices[IndiceCount]=i;
              IndiceCount++;
              }
            if (buffer[i]=='*') // ... and the "*"
              {
              indices[12]=i;
              IndiceCount++;
              }
            }
          // Load time data
          temp = indices[0];
          hour = (buffer[temp+1]-48)*10 + buffer[temp+2]-48;
          minute = (buffer[temp+3]-48)*10 + buffer[temp+4]-48;
          second = (buffer[temp+5]-48)*10 + buffer[temp+6]-48;
          temp = indices[1]; 
          
          if (buffer[temp+1] == 65) // Check for "A", ie GPS data valid
            {
            validGPSflag = 1; 
            lcd.setCursor(0,1);
              lcd.print("               ");
            }             
          else
            {
            validGPSflag = 0;
            }        
        } 
      counter=0;   // Reset the buffer
      for (int i=0;i<300;i++)
      { 
        buffer[i]=' '; 
      }

    } 
  } 
}
