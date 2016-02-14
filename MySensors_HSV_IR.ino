/*
	MySensors HSL IR - Version 1.0
	Copyright 2016 Francois Dechery
*/
#include <MySensor.h>
#include <SPI.h>
#include <FastLED.h>
//#include <IRremote.h>
#include <IRremoteNEC.h>
#include <DallasTemperature.h>
#include <OneWire.h>

#define NODE_ID 199		// 255 for Auto
#define CHILD_RGB_ID 1
#define CHILD_MODE_ID 2
#define CHILD_SPEED_ID 3
#define CHILD_TEMP_ID 4
#define CHILD_LDR_ID 5
#define INFO_NAME "RGB IR BedRoom"
#define INFO_VERS "1.0"

#define ONEWIRE_PIN 2 
#define IR_PIN 4
#define GREEN_PIN 5
#define BLUE_PIN 6
#define LED_PIN 7
#define CE_PIN 8
#define RED_PIN 9		
#define CS_PIN 10
#define LDR_PIN 14 // A0 pin

#define SPEED_STEP 20

#define ANIM1_SPEED 350		// flash ON Variable
#define ANIM1_PAUSE 200		// flash OFF fixed
#define ANIM2_SPEED 550		// strobe OFF variable
#define ANIM2_PAUSE 150		// storbe ON fixed
#define ANIM3_SPEED 100		// fade speed
#define ANIM4_SPEED 700	// smooth speed

#define TEMP_INTERVAL 91000		// (1:31 min) temperature is sent at this interval (ms)
//#define TEMP_INTERVAL 8000	// temperature is sent at this interval (ms)

#define LDR_INTERVAL 59000		// (1 min) luminosity is send at this interval (ms)

#define LED_DURATION 70		// Status led ON duration

// internal
#define DALLAS_CONVERT_TIME 751	// Depends on DS18B20  resolution 94, 188, 375, 750

#define BUTTONS_COUNT 24

unsigned long RbutCodes[]={
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

unsigned long RbutColors[]={
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
//int16_t 		temp_conversion_time =0;
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

MyMessage msg_rgb(CHILD_RGB_ID,		V_RGB);
//MyMessage msg_state(CHILD_RGB_ID,V_VAR1);
MyMessage msg_mode(CHILD_MODE_ID,	V_VAR1);
MyMessage msg_speed(CHILD_SPEED_ID,	V_VAR2);

MyMessage msg_temp(CHILD_TEMP_ID,	V_TEMP);
MyMessage msg_ldr(CHILD_LDR_ID,	V_LIGHT_LEVEL);


// ----------------------------------------------
void setup() {
	Serial.begin(115200);
	delay(100);
	Serial.println(" ");
	Serial.println("### Booting...");

	pinMode(RED_PIN, OUTPUT);		
	pinMode(GREEN_PIN, OUTPUT);
	pinMode(BLUE_PIN, OUTPUT);

	pinMode(LED_PIN, OUTPUT);

	irrecv.enableIRIn(); // Start the receiver

    dallas.begin();
    dallas.getAddress(temp_address, 0);
	dallas.setResolution(temp_address,12);
    dallas.setWaitForConversion(false);

	gw.begin(incomingMessage, NODE_ID, false);
	gw.sendSketchInfo(INFO_NAME, INFO_VERS);
	gw.present(CHILD_RGB_ID, 	S_RGB_LIGHT);
	gw.present(CHILD_MODE_ID,	S_CUSTOM);
	gw.present(CHILD_SPEED_ID,	S_CUSTOM);
	gw.present(CHILD_TEMP_ID,	S_TEMP);
	gw.present(CHILD_LDR_ID, 	S_LIGHT_LEVEL);
	
	confirmRgb();
	//bitSet(TCCR1B, WGM12);

 	//Request the last stored colors settings
	//gw.request(CHILD_RGB_ID, V_VAR1);

	Serial.println("### Boot complete !");
}


// ----------------------------------------------
void loop() {
	gw.process();
	processIr();
	processSensors();
	animationUpdate();
	ledUpdate();
}

// ----------------------------------------------
void processSensors(){

	// dallas Sensor -------------
	if(millis() > last_temp_time + TEMP_INTERVAL ){

		// wait that sensor is ready (DALLAS_CONVERT_TIME) to send the lat measure (not the previous one) 
		if(!temp_converting){
			//Serial.println("Requesting temp");
		    dallas.requestTemperatures(); // Send the command to get temperatures
		    temp_converting=true;
		    return;
		}
		else if(millis() < last_temp_time + TEMP_INTERVAL + DALLAS_CONVERT_TIME){
			//Serial.print("t");
			return;
		}
		//Serial.println();

		// Get the Temperature
		temp_converting=false;
		
		float this_temp = dallas.getTempC(temp_address); 
		//float cur_temp = dallas.getTempCByIndex(0);

		// if Temperature is correctly defined
	    if (! isnan(this_temp)) {
			int cur_temp =  (int) (this_temp * 10 )  ; //rounded to 1 dec

			Serial.print("Tempp * 10 = "); Serial.println(cur_temp);

	    	// Send only if temperature has changed
	    	if (cur_temp != last_temp  && cur_temp != -1270 && cur_temp != 850) {
	    		gw.send(msg_temp.set( cur_temp / 10.0 , 1));
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

		Serial.print("Light is : "); Serial.println(cur_ldr);

	    // Send only if luminosity has changed
		if(cur_ldr != last_ldr){
	    	gw.send(msg_ldr.set(cur_ldr, 1 ));
			last_ldr		= cur_ldr;
			last_ldr_time	= millis();			
		}
		else{
	      	//retry in 17s
	    	last_ldr_time = last_ldr_time + 17000;
		}
	}
}

// ----------------------------------------------
void processIr() {
	if (irrecv.decode(&results)) {
		//Serial.println(results.value, HEX);
		//dump(&results);
		unsigned long code = results.value;
		if( code == 0xFFFFFFFF){
			Serial.print("Repeat ");
			code = last_ir_button;
		}
		processIrButtons(code);
		irrecv.resume(); // Receive the next value
	}
}

// ----------------------------------------------
void processIrButtons(unsigned long code) {
	//Serial.print(results->value, HEX);
	boolean done=false;
	for (int i = 0; i < BUTTONS_COUNT ; i = i + 1) {
		if( code == RbutCodes[i] ){
			//Serial.print(" : ");
			//Serial.print(RbutColors[i], HEX);
			Serial.print(" -> ");

			last_ir_button = 0; //no repat else if specified
				
			if(i == 0){
				buttonBrightness(true);
				last_ir_button = code;
				delay(150); //debounce
	    		//gw.send(msg_temp.set(cur_temp, 1));

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
		}
	}
	if(!done){
		Serial.print(code,HEX);
		Serial.println(" ...");
	}
}

// ----------------------------------------------
void SendLastColorStatus(){
  //String cStatus=CHSV(current_color) + String("&") + String(current_color.v) + String("&") + String(current_status);
  //gw.send(msg_state.set(cStatus.c_str()));
}

// ----------------------------------------------
void buttonPower(boolean on){
	ledOn();
	Serial.print("Button Power : ");
	current_anim=0;
	if(on){
		current_status=1;
		Serial.print("ON");
		Serial.println();
		setLeds(current_color);
	}
	else{
		current_status=0;
		Serial.print("OFF");
		Serial.println();
		setLeds(CHSV {0,0,0});
	}
	Serial.println();
}

// ----------------------------------------------
void buttonSpecial(byte but){
	ledOn();
	Serial.print("Button Special : ");
	Serial.print(but);
	Serial.println();
	animation(but, true);
}

// ----------------------------------------------
void buttonBrightness(boolean up){
	Serial.print("Button Brightness : ");
	if(up){
		if(current_anim==0){
			Serial.println("UP");
			buttonColor(current_color, 1 );
		}
		else{
			Serial.println("FASTER");
			buttonChangeSpeed( - SPEED_STEP );
		}
	}
	else{
		if(current_anim==0){
			Serial.println("DOWN");
			buttonColor(current_color, -1 );
		}
		else{
			Serial.println("SLOWER");
			buttonChangeSpeed( SPEED_STEP );
		}
	}
	Serial.println();
}

// ----------------------------------------------
void buttonColor(CHSV color, int offset){
	ledOn();
	Serial.print("Button Color : ");
	color=dimHSV(color,offset);
	setLeds(color);
	current_status=1;
	current_color=color; 
}

// ----------------------------------------------
void buttonChangeSpeed(int offset){
	ledOn();
	if(offset !=0){
		current_speed=current_speed + offset ;
	}
}

// ----------------------------------------------
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

// ----------------------------------------------
void setBrightness(byte val, boolean convert){
	if(convert){
		val =map(val,0,100,0,255);
	}
	Serial.print("Setting Brightness to : ");
	Serial.println(val);
	current_color.v=val;
	buttonColor(current_color,0);	
}

// ----------------------------------------------
void incomingMessage(const MyMessage &message) {
	// We only expect one type of message from controller. But we better check anyway.
	Serial.println("--> Processing Incoming Message...");
	if (message.isAck()) {
		Serial.println("This is an ack from gateway");
		return;
	}

	if (message.type == V_RGB) {
		String color_string = message.getString();
		CRGB color = (long) strtol( &color_string[0], NULL, 16);
		buttonColor(RgbToHsv(color),0);
		
		// Store state in eeprom
		//gw.saveState(0, state);
	
		//Serial.print("Incoming change for sensor:");
		//Serial.print(message.sensor);
		//Serial.print(", New status: ");
		//Serial.println(message.getBool());
	} 
	else if (message.type == V_STATUS){
		buttonPower(message.getBool());
	}
	else if (message.type == V_DIMMER){
		//Serial.println("Dimming 0-100% currently not handled !");
		setBrightness(message.getByte(), true);
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
		Serial.println("WARNING : message is not handled !");
	}
}

// ----------------------------------------------
void setSpeed(unsigned long speed){
	if(speed !=0){
		current_speed=speed ;
	}
}

// ----------------------------------------------
void setLedsAnalog(CRGB rgb){
	analogWrite(RED_PIN, rgb.r);
	analogWrite(GREEN_PIN, rgb.g);
	analogWrite(BLUE_PIN, rgb.b);	

	Serial.print(" --> RGB=");
	Serial.print(rgb.r);
	Serial.print(",");
	Serial.print(rgb.g);
	Serial.print(",");
	Serial.print(rgb.b);
	Serial.println();
}

// ----------------------------------------------
//void setLeds(const CRGB& rgb){
void setLeds(CHSV hsv){
	Serial.print(" --> ( HSV=");
	Serial.print(hsv.h);
	Serial.print(",");
	Serial.print(hsv.s);
	Serial.print(",");
	Serial.print(hsv.v);
	Serial.print(" ) ");
	setLedsAnalog( CHSV(hsv) );
}

// ----------------------------------------------
void confirmFlash(){
	setLeds(CHSV {0,0,0});
	gw.wait(100);

	setLeds(CHSV {0,0,100});
	gw.wait(80);

	setLeds(CHSV {0,0,0});
	gw.wait(80);

	setLeds(RgbToHsv(CRGB::White));
	gw.wait(80);

	setLeds(CHSV {0,0,0});
	gw.wait(80);
}

// ----------------------------------------------
void confirmRgb(){
	setLeds(CHSV {0,0,0});
	gw.wait(100);

	setLeds(CHSV {0,255,100});
	gw.wait(300);

	setLeds(CHSV {85,255,100});
	gw.wait(300);

	setLeds(CHSV {171,255,100});
	gw.wait(300);

	setLeds(CHSV {0,0,0});
}

// ----------------------------------------------
void animation(byte mode, boolean init){
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
			setLeds(current_color);
			current_step=0;
			last_update = now;
		}
		else if(current_step==0 && now > (last_update + ANIM1_PAUSE) ){
			setLeds(CHSV {0,0,0});
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
			setLeds(current_color);
			current_step=0;
			last_update = now;
		}
		else if(current_step==0 && now > (last_update + current_speed) ){
			setLeds(CHSV {0,0,0});
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
			setLeds( CHSV(current_color.h, current_color.s, dim8_lin(current_step)) );
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
			setLeds( CHSV(current_step,255,255) );
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

// ----------------------------------------------
void animationUpdate(){
	animation(current_anim, false);
}

// ----------------------------------------------
void ledUpdate(){
	if(millis() > last_led_time + LED_DURATION  ){
		digitalWrite(LED_PIN, LOW);
	}
}

// ----------------------------------------------
void ledOn(){
	digitalWrite(LED_PIN, HIGH);
	last_led_time=millis();
}

//-----------------------------------------------
CHSV RgbToHsv(CRGB rgb){
     return rgb2hsv_approximate(rgb);
}




/*
// ----------------------------------------------
CRGB longToRgb(unsigned long rgb){
	CRGB out;
	out.r = rgb >> 16;
	out.g = rgb >> 8 & 0xFF;
	out.b = rgb & 0xFF;
	return out;
}

// ----------------------------------------------
unsigned long rgbToLong(CRGB in){
	return (((long)in.r & 0xFF) << 16) + (((long)in.g & 0xFF) << 8) + ((long)in.b & 0xFF);
}
*/






/*
void dump(decode_results *results) {
	// Dumps out the decode_results structure.
	// Call this after IRrecv::decode()
	int count = results->rawlen;
 if (results->decode_type == NEC) {
		Serial.print("Decoded NEC: ");
	}
	Serial.print(results->value, HEX);
	Serial.print(" (");
	Serial.print(results->bits, DEC);
	Serial.println(" bits)");
}
*/
