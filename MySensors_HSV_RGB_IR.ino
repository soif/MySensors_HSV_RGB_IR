/*
	MySensors HSV IR - Version 0.3
	Copyright 2016 Francois Dechery

	https://media.readthedocs.org/pdf/mysensors/latest/mysensors.pdf
*/

// Includes #############################################################################

#include <MySensor.h>
#include <SPI.h>
#include <FastLED.h>
//#include <IRremote.h>
#include <IRremoteNEC.h>
#include <DallasTemperature.h>
#include <OneWire.h>

// Defines ##############################################################################
#define INFO_NAME "HSV RGB IR Led Strip"
#define INFO_VERS "0.3"

#define NODE_ID 199		// 255 for Auto
#define CHILD_RGB_ID 1
#define CHILD_HSV_ID 2
#define CHILD_TEMP_ID 3
#define CHILD_LDR_ID 4

#define ONEWIRE_PIN 2 
#define IR_PIN 4
#define GREEN_PIN 5
#define BLUE_PIN 6
#define LED_PIN 7
#define CE_PIN 8
#define RED_PIN 9		
#define CS_PIN 10
#define LDR_PIN 14 				// A0 pin

#define SPEED_STEP 20

#define ANIM1_SPEED 350			// flash ON Variable
#define ANIM1_PAUSE 200			// flash OFF fixed
#define ANIM2_SPEED 550			// strobe OFF variable
#define ANIM2_PAUSE 150			// storbe ON fixed
#define ANIM3_SPEED 100			// fade speed
#define ANIM4_SPEED 700			// smooth speed

#define TEMP_INTERVAL 91000		// (1:31 min) temperature is sent at this interval (ms)
//#define TEMP_INTERVAL 8000	// temperature is sent at this interval (ms)

#define LDR_INTERVAL 59000		// (1 min) luminosity is send at this interval (ms)

#define LED_DURATION 70			// Status led ON duration

#define DALLAS_CONVERT_TIME 751	// DS18B20 conversion time, Depends on  resolution (9,10,11,12b) : 94, 188, 375, 750

#define ACKSEND false			// send ack to Gateway ?

#define BUTTONS_COUNT 24
unsigned long RbutCodes[]={		// IR remote buttons codes
	0xFF00FF, //	Brightness +
	0xFF807F, //	Brightness -
	0xFF40BF, //	OFF
	0xFFC03F, //	ON
	0xFF20DF, //	Red
	0xFFA05F, //	Green
	0xFF609F, //	Blue
	0xFFE01F, //	White
	0xFF10EF, //	R1
	0xFF906F, //	G1
	0xFF50AF, //	B1
	0xFFD02F, //	Flash
	0xFF30CF, //	R2
	0xFFB04F, //	G2
	0xFF708F, //	B2
	0xFFF00F, //	Strobe
	0xFF08F7, //	R3
	0xFF8877, //	G3
	0xFF48B7, //	B3
	0xFFC837, //	Fade
	0xFF28D7, //	R4
	0xFFA857, //	G4
	0xFF6897, //	B4
	0xFFE817		//	Smooth
};

unsigned long RbutColors[]={	// IR remote buttons colors
	0,			//	Brightness +
	0,			//	Brightness -
	0,			//	OFF
	0,			//	ON
	0xFF0000,	//	Red
	0x00FF00,	//	Green
	0x0000FF,	//	Blue
	0xFFFFFF,	//	White
	0xD13A01,	//	R1
	0x00E644,	//	G1
	0x0040A7,	//	B1
	0,			//	Flash
	0xE96F2A,	//	R2
	0x00BEBF,	//	G2
	0x56406F,	//	B2
	0,			//	Strobe
	0xEE9819,	//	R3
	0x00799A,	//	G3
	0x944E80,	//	B3
	0,			//	Fade
	0xFFFF00,	//	R4
	0x0060A1,	//	G4
	0xEF45AD,	//	B4
	0			//	Smooth
};
// debug ###############################################################################
#define MY_DEBUG	// Comment uncomment (May overflow ProMini memory when set)
#include "debug.h"

// variables declarations ###############################################################

CHSV 			current_color	= CHSV(0,255,255);
byte 			current_status  = 0 ;
byte 			current_anim  = 0 ;
byte 			current_step  = 0;
boolean 		current_dir	  = true;
unsigned long	current_speed = 1000;
unsigned long	last_update = millis();
unsigned long	last_ir_button=0;

int 			last_temp 		= 0;
unsigned long	last_temp_time	= 0;
boolean 		temp_converting 	=false;

byte			last_ldr		= 0;
unsigned long	last_ldr_time	= 0;

unsigned long	last_led_time	= 0;

IRrecv irrecv(IR_PIN); 		//IRrecv irrecv(IR_PIN, IR_LED_PIN); dont work. Why ?
decode_results results;

OneWire            oneWire(ONEWIRE_PIN);
DallasTemperature  dallas(&oneWire);
DeviceAddress		temp_address;

MyTransportNRF24 transport(CE_PIN, CS_PIN, RF24_PA_LEVEL);
MyHwATMega328 hw;
MySensor gw(transport,hw);

MyMessage msg_temp(CHILD_TEMP_ID,	V_TEMP);
MyMessage msg_ldr(CHILD_LDR_ID,		V_LIGHT_LEVEL);


// Here we start ########################################################################

// -----------------------------------------------------------------
void setup() {
	Serial.begin(115200);
	delay(100);
	DEBUG_PRINTLN();
	//DEBUG_PRINTLN("### Booting...");

	pinMode(RED_PIN, OUTPUT);		
	pinMode(GREEN_PIN, OUTPUT);
	pinMode(BLUE_PIN, OUTPUT);

	pinMode(LED_PIN, OUTPUT);

	irrecv.enableIRIn(); // Start the receiver

    dallas.begin();
    dallas.getAddress(temp_address, 0);
	dallas.setResolution(temp_address,12);
    dallas.setWaitForConversion(false);

	gw.begin(receiveMessage, NODE_ID, false);
	gw.sendSketchInfo(INFO_NAME, INFO_VERS);
	
	gw.present(CHILD_RGB_ID, 	S_RGB_LIGHT);
	gw.present(CHILD_HSV_ID, 	S_RGB_LIGHT);
	gw.present(CHILD_TEMP_ID,	S_TEMP);
	gw.present(CHILD_LDR_ID, 	S_LIGHT_LEVEL);
	
	confirmRgb();
	
	current_color = RomLoadColor();

	DEBUG_PRINTLN("####");
}

// -----------------------------------------------------------------
void loop() {
	gw.process();
	processIr();
	processSensors();
	updateAnimation();
	updateStatusLed();
}

// -----------------------------------------------------------------
void processIr() {
	if (irrecv.decode(&results)) {
		//DEBUG_PRINTLN(results.value, HEX);
		//dumpIR(&results);
		unsigned long code = results.value;
		if( code == 0xFFFFFFFF){
			DEBUG_PRINT("Repeat ");
			code = last_ir_button;
		}
		processIrButtons(code);
		irrecv.resume(); // Receive the next value
	}
}

// -----------------------------------------------------------------
void processSensors(){

	// dallas Sensor -------------
	if(millis() > last_temp_time + TEMP_INTERVAL ){

		// wait that sensor is ready (DALLAS_CONVERT_TIME) to send the lat measure (not the previous one) 
		if(!temp_converting){
			//DEBUG_PRINTLN("Requesting temp");
		    dallas.requestTemperatures(); // Send the command to get temperatures
		    temp_converting=true;
		    return;
		}
		else if(millis() < last_temp_time + TEMP_INTERVAL + DALLAS_CONVERT_TIME){
			//DEBUG_PRINT("t");
			return;
		}
		//DEBUG_PRINTLN('');

		// Get the Temperature
		temp_converting=false;
		
		float this_temp = dallas.getTempC(temp_address); 
		//float cur_temp = dallas.getTempCByIndex(0);

		// if Temperature is correctly defined
	    if (! isnan(this_temp)) {
			int cur_temp =  (int) (this_temp * 10 )  ; //rounded to 1 dec

			//DEBUG_PRINT("Temp * 10 = "); DEBUG_PRINTLN(cur_temp);

	    	// Send only if temperature has changed
	    	if (cur_temp != last_temp  && cur_temp != -1270 && cur_temp != 850) {
	    		gw.send(msg_temp.set( (float) cur_temp / 10.0 , ACKSEND));
	        	last_temp = cur_temp;
				last_temp_time= millis();
			}
			else{
	      		// retry in 33s
	    		last_temp_time = last_temp_time + 33000;
			}
	    }
	    //else temperatured bugged, retrying soon : in 13s
	    else{
	    	last_temp_time = last_temp_time + 13000;
	    }
	}
	
	// LDR sensor ---------------
	if( millis() > last_ldr_time + LDR_INTERVAL ){
		//map to 0-100%
		byte cur_ldr = map( analogRead(LDR_PIN), 0,1023, 0,100);

		//DEBUG_PRINT("LDR is : "); DEBUG_PRINTLN(cur_ldr);

	    // Send only if luminosity has changed
		if(cur_ldr != last_ldr){
	    	gw.send(msg_ldr.set(cur_ldr, ACKSEND ));
			last_ldr		= cur_ldr;
			last_ldr_time	= millis();			
		}
		else{
	      	//retry in 17s
	    	last_ldr_time = last_ldr_time + 17000;
		}
	}
}

// -----------------------------------------------------------------
void processIrButtons(unsigned long code) {
	//DEBUG_PRINT(results->value, HEX);
	boolean done=false;
	for (int i = 0; i < BUTTONS_COUNT ; i = i + 1) {
		if( code == RbutCodes[i] ){
			//DEBUG_PRINT(" : ");
			//DEBUG_PRINTHEX(RbutColors[i]);
			//DEBUG_PRINT(" -> ");

			last_ir_button = 0; //no repat else if specified
				
			if(i == 0){
				buttonBrightness(true);
				last_ir_button = code;
				delay(150); //debounce
			}		 
			else if(i == 1){
				buttonBrightness(false);
				last_ir_button = code;
				delay(150); //debounce
			}		 
			else if(i == 2){
				buttonPower(false);
			}		 
			else if(i == 3){
				buttonPower(true);
			}		 
			else if(i == 11){
				buttonSpecial(1);
			}		 
			else if(i == 15){
				buttonSpecial(2);
			}
			else if(i == 19){
				buttonSpecial(3);
			}		 
			else if(i == 23){
				buttonSpecial(4);
			}
			else{
				buttonColor(RgbToHsv(RbutColors[i]),0);
			}
			done=true;
			sendMessage();
		}
	}
	if(!done){
		//DEBUG_PRINTHEX(code);
		//DEBUG_PRINTLN(" ...");
	}
}

// -----------------------------------------------------------------
void sendMessage(){

	MyMessage msg(CHILD_RGB_ID,V_RGB);
	char buffer[7];
	long lc;
	
	// ---- RGB report -----------------------
	//msg.setSensor(CHILD_RGB_ID);
	//msg.setType(V_RGB);

	// color
	lc =rgbToLong( CHSV(current_color));
	ltoa(lc , buffer, 16);
	gw.send(msg.set( buffer ), ACKSEND);
	DEBUG_PRINT("RVB=");
	DEBUG_PRINTLN(buffer);

	// status
	msg.setType(V_STATUS);
	gw.send(msg.set( (boolean) current_status), ACKSEND);

	// Brightness
	if(current_status == 1){
		msg.setType(V_PERCENTAGE);
		gw.send(msg.set( map( current_color.v ,0,255, 0,100) ), ACKSEND);
	}

	// ---- HSV report ------------------------
	msg.setSensor(CHILD_HSV_ID);

	// color
	msg.setType(V_RGB); // wse should have a V_HSV type !!!

	lc =hsvToLong(current_color);
	ltoa(lc , buffer, 16);
	//gw.send(msg.set( buffer ), ACKSEND);
	DEBUG_PRINT("HSV=");
	DEBUG_PRINTLN(buffer);

	// status
	msg.setType(V_STATUS);
	gw.send(msg.set( (boolean) current_status), ACKSEND);

	// Brightness
	if(current_status == 1){
		msg.setType(V_PERCENTAGE);
		gw.send(msg.set( map( current_color.v ,0,255, 0,100) ), ACKSEND);
	}
}

// -----------------------------------------------------------------
void receiveMessage(const MyMessage &message) {
	// We only expect one type of message from controller. But we better check anyway.
	DEBUG_PRINTLN("-> MESSAGE");

	if (message.isAck()) {
		DEBUG_PRINTLN("Is ACK");
		return;
	}

	if (message.type == V_RGB && message.sensor == CHILD_RGB_ID ) {
		String color_string = message.getString();
		CRGB color = (long) strtol( &color_string[0], NULL, 16);
		buttonColor(RgbToHsv(color),0);
	} 
	else if (message.type == V_RGB && message.sensor == CHILD_HSV_ID ) {
		String color_string = message.getString();
		long color = (long) strtol( &color_string[0], NULL, 16);
		buttonColor(longToHsv(color),0);
	}
	else if (message.type == V_STATUS){
		buttonPower(message.getBool());
	}
	else if (message.type == V_PERCENTAGE){
		setBrightness(message.getByte());
	}
	else if (message.type == V_VAR1){
		// anim mode
		buttonSpecial(message.getByte());
	}
	else if (message.type == V_VAR2){
		//anim speed
		setSpeed(message.getULong());		
	}
	else{
		//DEBUG_PRINTLN("WARNING : message is not handled !");
	}
}

// -----------------------------------------------------------------
void buttonPower(boolean on){
	flashStatusLed();
	DEBUG_PRINT("BUT Power: ");
	current_anim=0;
	if(on){
		current_status=1;
		DEBUG_PRINTLN("ON");
		setLedsHSV(current_color);
	}
	else{
		current_status=0;
		DEBUG_PRINTLN("OFF");
		setLedsHSV(CHSV {0,0,0});
	}
	DEBUG_PRINTLN();
}

// -----------------------------------------------------------------
void buttonSpecial(byte but){
	flashStatusLed();
	DEBUG_PRINT("BUT Special: ");
	DEBUG_PRINTLN(but);
	processAnimation(but, true);
}

// -----------------------------------------------------------------
void buttonBrightness(boolean up){
	DEBUG_PRINT("Button Brightness: ");
	if(up){
		if(current_anim==0){
			DEBUG_PRINTLN("UP");
			buttonColor(current_color, 1 );
		}
		else{
			DEBUG_PRINTLN("FASTER");
			buttonChangeSpeed( - SPEED_STEP );
		}
	}
	else{
		if(current_anim==0){
			DEBUG_PRINTLN("DOWN");
			buttonColor(current_color, -1 );
		}
		else{
			DEBUG_PRINTLN("SLOWER");
			buttonChangeSpeed( SPEED_STEP );
		}
	}
	DEBUG_PRINTLN();
}

// -----------------------------------------------------------------
void buttonColor(CHSV color, int offset){
	flashStatusLed();
	DEBUG_PRINT("BUT Color: ");
	color=dimHSV(color,offset);
	setLedsHSV(color);
	current_status=1;
	current_color=color;
	RomSaveCurrentColor();
}

// -----------------------------------------------------------------
void buttonChangeSpeed(int offset){
	flashStatusLed();
	if(offset !=0){
		current_speed=current_speed + offset ;
	}
}

// -----------------------------------------------------------------
CHSV dimHSV(CHSV color, int offset){
	offset=offset*10;
	int bright=color.v + offset;
	if(bright < 1){
		bright=1; // no off
	}
	if(bright > 255){
		bright=255;
	}
	color.v=bright;
	return color;
}

// -----------------------------------------------------------------
void setBrightness(byte val){
	val =map(val,0,100,0,255);
	DEBUG_PRINT("Set Bright. : ");
	DEBUG_PRINTLN(val);
	current_color.v=val;
	if(val==0){
		current_status=0;
	}
	buttonColor(current_color,0);	
}

// -----------------------------------------------------------------
void setSpeed(unsigned long speed){
	if(speed !=0){
		current_speed=speed ;
	}
}

// -----------------------------------------------------------------
void setLedsRGB(CRGB rgb){
	analogWrite(RED_PIN, rgb.r);
	analogWrite(GREEN_PIN, rgb.g);
	analogWrite(BLUE_PIN, rgb.b);	

	DEBUG_PRINT(" - RGB=");
	DEBUG_PRINT(rgb.r);
	DEBUG_PRINT(",");
	DEBUG_PRINT(rgb.g);
	DEBUG_PRINT(",");
	DEBUG_PRINT(rgb.b);
	DEBUG_PRINTLN();
}

// -----------------------------------------------------------------
//void setLedsHSV(const CRGB& rgb){
void setLedsHSV(CHSV hsv){
	DEBUG_PRINT(" - HSV=");
	DEBUG_PRINT(hsv.h);
	DEBUG_PRINT(",");
	DEBUG_PRINT(hsv.s);
	DEBUG_PRINT(",");
	DEBUG_PRINT(hsv.v);
	setLedsRGB( CHSV(hsv) );
}

// -----------------------------------------------------------------
void confirmFlash(){
	setLedsRGB(CRGB::Black);
	gw.wait(80);

	setLedsRGB(CRGB::White);
	gw.wait(80);

	setLedsRGB(CRGB::Black);
	gw.wait(80);

	setLedsRGB(CRGB::White);
	gw.wait(80);

	setLedsRGB(CRGB::Black);
	gw.wait(80);
}

// -----------------------------------------------------------------
void confirmRgb(){
	setLedsRGB(CRGB::Black);
	gw.wait(200);

	setLedsRGB(CRGB::Red);
	gw.wait(400);

	setLedsRGB(CRGB::Lime);
	gw.wait(400);

	setLedsRGB(CRGB::Blue);
	gw.wait(400);

	setLedsRGB(CRGB::Black);
}

// -----------------------------------------------------------------
void processAnimation(byte mode, boolean init){
	if(init && current_anim == mode){
		current_anim=0;
		current_status=0;
		confirmFlash();
		return;
	}
	current_anim=mode;
	current_status=1;

	// anim1 : flash
	if(current_anim==1){
		if(init){
			current_speed = ANIM1_SPEED;
			current_step=1;
		}
		unsigned long now= millis();
		if(current_step==1 && now > (last_update + current_speed) ){
			setLedsHSV(current_color);
			current_step=0;
			last_update = now;
		}
		else if(current_step==0 && now > (last_update + ANIM1_PAUSE) ){
			setLedsHSV(CHSV {0,0,0});
			current_step=1;
			last_update = now;
		}
	}

	// anim2 : strobe
	else if(current_anim==2){
		if(init){
			current_speed = ANIM2_SPEED;
			current_step=1;
		}
		unsigned long now= millis();
		if(current_step==1 && now > (last_update + ANIM2_PAUSE) ){
			setLedsHSV(current_color);
			current_step=0;
			last_update = now;
		}
		else if(current_step==0 && now > (last_update + current_speed) ){
			setLedsHSV(CHSV {0,0,0});
			current_step=1;
			last_update = now;
		}
	}

	// anim3 : fade
	else if(current_anim==3){
		if(init){
			current_speed = ANIM3_SPEED;
			current_step =current_color.v;
		}

		unsigned long now= millis();
		if( now > (last_update + current_speed) ){
			setLedsHSV( CHSV(current_color.h, current_color.s, dim8_lin(current_step)) );
			if(current_dir){
				if(current_step == 255){
					current_dir=false;
				}
				else{
					current_step++;
				}
			}
			else{
				if(current_step == 1){
					current_dir=true;
				}
				else{
					current_step--;
				}
			}
			last_update = now;
		}
	}

	// anim4 : smooth
	else if(current_anim==4){
		if(init){
			current_speed = ANIM4_SPEED;
			current_step =0;
		}
		unsigned long now= millis();
		if( now > (last_update + current_speed) ){
			setLedsHSV( CHSV(current_step,255,255) );
			current_step++;
			last_update = now;
			if(current_step > 255){
				current_step=0;
			}
		}
	}
	else{
		//invalid mode
		current_anim=0;
		current_status=0;
	}
}

// -----------------------------------------------------------------
void updateAnimation(){
	processAnimation(current_anim, false);
}

// -----------------------------------------------------------------
void updateStatusLed(){
	if(millis() > last_led_time + LED_DURATION  ){
		digitalWrite(LED_PIN, LOW);
	}
}

// -----------------------------------------------------------------
void flashStatusLed(){
	digitalWrite(LED_PIN, HIGH);
	last_led_time=millis();
}

// -----------------------------------------------------------------
CHSV RgbToHsv(CRGB rgb){
     return rgb2hsv_approximate(rgb);
}

// -----------------------------------------------------------------
unsigned long hsvToLong(CHSV in){
	return (((long)in.h & 0xFF) << 16) + (((long)in.s & 0xFF) << 8) + ((long)in.v & 0xFF);
}

// -----------------------------------------------------------------
unsigned long rgbToLong(CRGB in){
	return (((long)in.r & 0xFF) << 16) + (((long)in.g & 0xFF) << 8) + ((long)in.b & 0xFF);
}

// -----------------------------------------------------------------
void RomSaveCurrentColor(){
	//save it to 1,2,3 positions
	//but dont stress eeprom if not needed
	CHSV rom =RomLoadColor();
	if(current_color.h != rom.h){gw.saveState(1,current_color.h);}
	if(current_color.s != rom.s){gw.saveState(2,current_color.s);}
	if(current_color.v != rom.v){gw.saveState(3,current_color.v);}
}

// -----------------------------------------------------------------
CHSV RomLoadColor(){
	//load from 1,2,3 positions
	CHSV color;
	color.h=gw.loadState(1);
	color.s=gw.loadState(2);
	color.v=gw.loadState(3);
	return color;
}

// ----------------------------------------------
CHSV longToHsv(unsigned long hsv){
	CHSV out;
	out.h = hsv >> 16;
	out.s = hsv >> 8 & 0xFF;
	out.v = hsv & 0xFF;
	return out;
}












/*

// Helpful Gizmocuz RVB example #################################################################

//https://www.domoticz.com/forum/viewtopic.php?t=8039#p57521
  gw.request(CHILD_ID, V_VAR1);


strcpy(actRGBvalue, getValue(szMessage,'&',0).c_str());
actRGBbrightness=atoi(getValue(szMessage,'&',1).c_str());
actRGBonoff=atoi(getValue(szMessage,'&',2).c_str());

void SendLastColorStatus(){
  String cStatus=actRGBvalue+String("&")+String(actRGBbrightness)+String("&")+String(actRGBonoff);
  gw.send(lastColorStatusMsg.set(cStatus.c_str()));
}

String getValue(String data, char separator, int index){
 int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;
  for(int i=0; i<=maxIndex && found<=index; i++){
  if(data.charAt(i)==separator || i==maxIndex){
  found++;
  strIndex[0] = strIndex[1]+1;
  strIndex[1] = (i == maxIndex) ? i+1 : i;
  }
 }
  return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}






// OLD ##################################################################################
// ----------------------------------------------
CRGB longToRgb(unsigned long rgb){
	CRGB out;
	out.r = rgb >> 16;
	out.g = rgb >> 8 & 0xFF;
	out.b = rgb & 0xFF;
	return out;
}


// ----------------------------------------------
void dumpIR(decode_results *results) {
	// Dumps out the decode_results structure.
	// Call this after IRrecv::decode()
	int count = results->rawlen;
 if (results->decode_type == NEC) {
		DEBUG_PRINT("Decoded NEC: ");
	}
	DEBUG_PRINT(results->value, HEX);
	DEBUG_PRINT(" (");
	DEBUG_PRINT(results->bits, DEC);
	DEBUG_PRINT(" bits)");
}
*/
