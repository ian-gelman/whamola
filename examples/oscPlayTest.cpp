#include "mbed.h"
#include "EthernetInterface.h"
#include "osc_client.h"
#include "ODriveMbed.h"
#include "RotaryActuator.h"
#include <string>

#define VERBOSE 1
#define TEST 0

#define ODRIVE2TX D1
#define ODRIVE2RX D0

char* instrumentName;

//Setup digital outputs
DigitalOut led1(LED1);	//Green running LED
DigitalOut led2(LED2);	//Blue controller LED
DigitalOut led3(LED3);	//Red ethernet LED

// pc UART comms constructor
Serial pc(USBTX, USBRX);

// User button for test program flow
DigitalIn userButton(USER_BUTTON);

// ODrive constructor
Serial odrive_serial(ODRIVE2TX, ODRIVE2RX);
ODriveMbed odrive(odrive_serial);


// Function Prototypes
void calibrateODrive();
void calibrateStrikers();

// Init variables
int loopTime = 3; // milliseconds
Thread thread(osPriorityHigh);
EventQueue queue;

// Right striker Actuator Init
RotaryActuator StrikerR(new QEIx4(PD_4, PD_3, NC, (QEIx4::EMODE)(QEIx4::IRQ | QEIx4::SPEED)),
						new SingleMC33926MotorController(D8, D14, A2, PB_10, PE_15, false), 
						loopTime, 0.0005f, 0.0f, 0.0001f);
// SingleMC33926MotorController * rightStriker =  new SingleMC33926MotorController(D8, D14, A2, PB_10, PE_15, false);


// Left striker Actuator Init
RotaryActuator StrikerL(new QEIx4(PD_6, PD_5, NC, (QEIx4::EMODE)(QEIx4::IRQ | QEIx4::SPEED)),
						new SingleMC33926MotorController(D7, D13, A1, PE_14, PB_11, false), 
						loopTime, 0.0005f, 0.0f, 0.0001f);


// Dampener Actuator Init
RotaryActuator Dampener(new QEIx4(PD_1, PD_0, NC, (QEIx4::EMODE)(QEIx4::IRQ | QEIx4::SPEED)),
						new SingleMC33926MotorController(D11, D15, A0, D3, D5, true), 
						loopTime, 0.001f, 0.0f, 0.00012f);



/**
 * Main function
 */
int main() {
	pc.baud(115200);
	odrive_serial.baud(115200);
	//set the instrument name

	while(!userButton){}

	// Calibrate the ODrive and strikers
	calibrateODrive();
	calibrateStrikers();

	char name[] = "Whamola";
	instrumentName = name;

	printf("Starting Instrunemt\r\n");

	//Turn on all LEDs
	led1 = 1;
	led2 = 2;
	led3 = 3;

	//Setup ethernet interface
	EthernetInterface eth;
	eth.connect();
	if(VERBOSE) printf("Client IP Address:     %s\r\n", eth.get_ip_address());
	led3 = 0;	//turn off red LED

	//Setup OSC client
	OSCClient osc(&eth, instrumentName);
	osc.connect();
	if(VERBOSE) printf("OSC controller IP Address: %s\r\n", osc.get_controller_ip());
	led2 = 0;	//turn off blue LED
	
	//Create a pointer to an OSC Message
	OSCMessage* msg = (OSCMessage*) malloc(sizeof(OSCMessage));

	//Variable to store the OSC message size
	unsigned int size;
	
	//Blocking Example
    while(true) {
        
        //get a new message and the size of the message
		osc.waitForMessage(msg);

		//Check that the message is for this instrument
		if(strcmp(osc.getInstrumentName(msg), instrumentName) == 0) {

			printf("Message for this instrument\r\n");

			//Process the message based on type
			char* messageType = osc.getMessageType(msg);

			 // Play a specific note
			if(strcmp(messageType, "play") == 0) {

				//printf("Is a play message\r\n");

				// Check that the messages is the right format
				if(strcmp(msg->format, ",ii") == 0) { // two ints
					
					uint32_t pitch 	  = osc.getIntAtIndex(msg, 0);
					uint32_t velocity = osc.getIntAtIndex(msg, 1);

					printf("Pitch %d, velocity %d\r\n",pitch,velocity);

					// TODO: command ODrive position to move to a given note

					if(velocity !=0){ // Strike the String
						float motorPower = (float) velocity/100.0;
						static bool i = true;
						if(i){
							StrikerL.coastStrike(motorPower, 350, 500);
						}
						else{
							StrikerR.coastStrike(motorPower, 350, 500);

						}
						i = !i;
						led2 = 1;
					}
					else{ // Dapen the string
						Dampener.dampenString(&queue, true, 200); // Damps the string for 200 ms
						led2 = 0;

					}
					//TODO: play the note

				}
			}
			// Turn off all notes
			else if(strcmp(messageType, "allNotesOff") == 0) {

				Dampener.dampenString(&queue, true, 500); // Damps the string for 500 ms

				//TODO: All notes off

			}
		}
		else {
			//Not intended for this instrument
		}

		printf("\r\n");
    }
    return 0;
}


void calibrateStrikers(){

	Dampener.setCurrentOffsetThreshold(0.005f);

    // while(!stopButton);

	// // Start the event queue's dispatch thread
	thread.start(callback(&queue, &EventQueue::dispatch_forever));

	// attach the Striker and Dampener controlLoop functions to the EventQueue to repeat on the loop time
	queue.call_every(loopTime, &StrikerR, &RotaryActuator::controlLoop);
	queue.call_every(loopTime, &StrikerL, &RotaryActuator::controlLoop);
	queue.call_every(loopTime, &Dampener, &RotaryActuator::controlLoop);

	pc.puts("calibrating right striker now...\n"); // TODO: Figure out why right striker encoder is not being read
	StrikerR.calibrate(700);
	pc.puts("Done calibrating right striker now\n");
	
	pc.puts("calibrating left striker now...\n");
	StrikerL.calibrate(700);
	pc.puts("Done calibrating left striker\n");

	pc.puts("calibrating dampener now...\n");
	Dampener.calibrate(400);
	pc.puts("Done calibrating dampener\n");

	if(TEST){
		wait(3);
		StrikerL.coastStrike(0.75, 350, 200);
		wait(3);
		StrikerR.coastStrike(0.75, 350, 200);
		wait(3);
		Dampener.dampenString(&queue, true, 500);
		wait(3);
	}
}

// TODO: put this in a proxy class that interfaces with the ODriveMbed library directly
void calibrateODrive(){
	int axis = 0;

	pc.puts("Checking ODrive now...\n");
	if(!odrive.setControlMode(axis, ODriveMbed::CTRL_MODE_POSITION_CONTROL, true))
		pc.printf("something went wrong and the control mode was not successfully set, current mode is a %d\n", odrive.readControlMode(axis));
	else
		pc.printf("control mode set to: %d\n", odrive.readControlMode(axis));

	if(TEST){
		odrive.setPosition(axis, 10000);
		wait(3);
		odrive.setPosition(axis, 0);
		wait(0.1);
		odrive.setPosition(axis, 10000);
		wait(0.1);
		odrive.setPosition(axis, 0);
		wait(0.1);
	}
	odrive.setPosition(axis, 10000);

	// TODO: Add calbration sequence that will find different note values
}