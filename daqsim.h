#define DAQ_NUM_DIGITAL_OUTPUTS 6
#define DAQ_NUM_DISPLAY_OUTPUTS 8
#define DAQ_NUM_DIGITAL_INPUTS 4
#define DAQ_NUM_ANALOG_INPUTS 2

void daq_sleep(int milliseconds); 

int daq_digital_read(int channel_number);
void daq_digital_write(int channel_number, int value);
void daq_display_write(int channel_number, unsigned char value);
double daq_analog_read(int channel_number);

int daq_continue_loop(void);
int daq_setup(int setup_num);

int setupDAQ(int setupNum);
int continueSuperLoop(void);
void digitalWrite(int channelNumber, int val);
int digitalRead(int channelNumber);
void displayWrite(int data, int position);
double analogRead(int channelNumber);