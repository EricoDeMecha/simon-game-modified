#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include "daqsim.h"

#define LEDs 4

// random numbers
int randInt(int lower, int upper){
    // pseudo-random integer generator
    return ((rand() % (upper - lower + 1)) + lower);
}

void generateSequence(int length, int data[]){
    // generate a sequence of N pseudo random integers
    for(int i = 0; i < length; i++){
        data[i] = randInt(0,3); // 0-3 to represent the indexes of the leds
    }
}
void resetLEDs(){
    // reset leds
    for (int j = 0; j < LEDs; ++j) {
        digitalWrite(j, 0);
    }
}

void waitForClick(int clicked_button_st, int  pin){
    // set the end time to the current time plus 5 seconds
    time_t endTime = time(NULL) + 5;
    int clicked_button_st_rt = digitalRead(pin); // read from odd channels
    while (time(NULL) < endTime)
    {
        clicked_button_st = digitalRead(pin); // read from odd channels
        if(clicked_button_st != clicked_button_st_rt){
            break;
        }
    }
    // end of loop waiting for results
}
int main(void) {
    // seed the generator  should only be called once
    srand((unsigned)time(0));
    // Initialize the sequence counter with the lower possible size of the array
    int s_counter = 1;
    // setup the DAQ display
	setupDAQ(6); // this is to display UI( simon game uses 6 with 4 I&O)

	//Main loop to maintain the gpu
	while (continueSuperLoop()) {
		// Sleep to avoid high CPU usage.
		daq_sleep(1000);
		// generate sequence
		int data[s_counter];
		generateSequence(s_counter, data);
		// display the  sequence
		for(size_t i = 0; i < s_counter; i++){
		    digitalWrite(data[i], 1); // flash the LEDs in a sequence one by one
		    daq_sleep(500);
            digitalWrite(data[i], 0); // flash the LEDs in a sequence one by one
            daq_sleep(500);
		}
		// reset leds
		resetLEDs();
		// read the buttons
        // modify the array to include the real locations
        for(size_t i = 0; i < s_counter; i++){
            data[i] = (data[i] + 1); // changing the values to actual locations
        }

        // process clicks
        bool  fail_check = false;
		for(size_t i = 0; i < s_counter; i++){
		    if(data[i] % 2 != 0){ // odd
		        int clicked_button_st = 0;
                int btn = (data[i] - 1 );
                waitForClick((int) &clicked_button_st, btn);
                if(clicked_button_st == 0){
                    fail_check = false;
                }
		    }else{
                int clicked_button_st2 = 0;
		        int btn2 = (data[i] - 1 );
		        waitForClick((int) &clicked_button_st2, btn2);
                if (clicked_button_st2 == 1){
                    fail_check = true;
                }
		    }
		}
		if(!fail_check){ // success blink blue 3 times
		    for( int i = 0; i < 3; i++){
		        digitalWrite(3, 1);
		        daq_sleep(500);
                digitalWrite(3, 0);
                daq_sleep(500);
		    }
		    s_counter = (s_counter++ <= 5) ? ( s_counter++ ): 1 ; // else start the game
		}else{ // failure blinks read blue times
            for( int i = 0; i < 3; i++){
                digitalWrite(2, 1);
                daq_sleep(500);
                digitalWrite(2, 0);
                daq_sleep(500);
            }
            // s_counter retains the original value
		}
        // resets all the leds before next loop
        resetLEDs();
	}
	return 0;
};