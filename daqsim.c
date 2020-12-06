/*
The following license only applies to this file. Please note
that the source code for the Mongoose web server, distributed with this
is GPL-licensed.

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org>
*/

#define _CRT_SECURE_NO_WARNINGS

#include <stdlib.h>
#include <time.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#elif _POSIX_C_SOURCE >= 199309L
#else
#include <unistd.h> // for usleep
#endif

#include "mongoose.h"
#include "daqsim.h"
#include "browsercontent.h"

//static variables
static int digital_outputs[DAQ_NUM_DIGITAL_OUTPUTS] = { 0 };
static int digital_outputs_sent[DAQ_NUM_DIGITAL_OUTPUTS] = { 0 };

static unsigned char display_outputs[DAQ_NUM_DISPLAY_OUTPUTS] = { 0 };
static unsigned char display_outputs_sent[DAQ_NUM_DISPLAY_OUTPUTS] = { 0 };

static int digital_inputs[DAQ_NUM_DIGITAL_INPUTS] = { 0 };
static double analog_inputs[DAQ_NUM_ANALOG_INPUTS] = { 0 };

static int user_has_quit = 0;
static int user_has_setup = 0;
static char session_id[9];
static int setup_num = 0;

//#define DEBUG_MESSAGES

void daq_sleep(int milliseconds) {
#ifdef WIN32
	Sleep(milliseconds);
#elif _POSIX_C_SOURCE >= 199309L
	struct timespec ts;
	ts.tv_sec = milliseconds / 1000;
	ts.tv_nsec = (milliseconds % 1000) * 1000000;
	nanosleep(&ts, NULL);
#else
	usleep(milliseconds * 1000);
#endif
}

void check_for_setup(void) {
	if (!user_has_setup) printf("Error: Cannot call DAQ functions without first calling setupDAQ.\n");
}

//unsafe DAQ i/o functions
//You must make sure the channel number is valid when using these.
int daq_digital_read(int channel_number) {
	return digital_inputs[channel_number] & 1;
}

void daq_digital_write(int channel_number, int value) {
	digital_outputs[channel_number] = value & 1;
}

void daq_display_write(int channel_number, unsigned char value) {
	display_outputs[channel_number] = value;
}

double daq_analog_read(int channel_number) {
	return analog_inputs[channel_number];
}

//HTTP server functions. Should not be called by user.
void send_outputs(struct mg_connection* connection, int force_send_all) {
	int hasChanged = 0;
	int num_outputs_changed = 0;
	int digital_outputs_changed[DAQ_NUM_DIGITAL_OUTPUTS] = { 0 };
	int display_outputs_changed[DAQ_NUM_DISPLAY_OUTPUTS] = { 0 };
	int i;
	if (force_send_all) {
		num_outputs_changed = DAQ_NUM_DIGITAL_OUTPUTS + DAQ_NUM_DISPLAY_OUTPUTS;
	}
	else {
		for (i = 0; i < DAQ_NUM_DIGITAL_OUTPUTS; i++) {
			register int output = digital_outputs[i];
			if (output != digital_outputs_sent[i]) {
				num_outputs_changed++;
				digital_outputs_changed[i] = 1;
				digital_outputs_sent[i] = output;
			}
		}
		for (i = 0; i < DAQ_NUM_DISPLAY_OUTPUTS; i++) {
			register unsigned char output = display_outputs[i];
			if (output != display_outputs_sent[i]) {
				num_outputs_changed++;
				display_outputs_changed[i] = 1;
				display_outputs_sent[i] = output;
			}
		}
	}
	if (num_outputs_changed > 0) {
		mg_printf(connection, "HTTP/1.0 200 OK\r\nContent-Length: %d\r\nCache-Control: no-store\r\n"
			"Content-Type: text/plain\r\n\r\n", num_outputs_changed * 9);
		for (i = 0; i < DAQ_NUM_DIGITAL_OUTPUTS; i++) {
			if (digital_outputs_changed[i] || force_send_all) {
				mg_printf(connection, "do %02x %02x\n", i, digital_outputs_sent[i]);
			}
		}
		for (i = 0; i < DAQ_NUM_DISPLAY_OUTPUTS; i++) {
			if (display_outputs_changed[i] || force_send_all) {
				mg_printf(connection, "ds %02x %02x\n", i, display_outputs_sent[i]);
			}
		}
		mg_printf(connection, "\n%c", '\0');
#ifdef DEBUG_MESSAGES
		printf("Outgoing HTTP message:\n%s", connection->send_mbuf.buf);
#endif
		connection->flags |= MG_F_SEND_AND_CLOSE;
	}
}

void send_forbidden(struct mg_connection* connection) {
	char* buf = "you aren't allowed to do that";
	mg_printf(connection, "HTTP/1.0 403 FORBIDDEN\r\nContent-Length: %d\r\nCache-Control: no-store\r\n"
		"Content-Type: text/plain\r\n\r\n%s",
		(int)strlen(buf), buf);
	connection->flags |= MG_F_SEND_AND_CLOSE;
}

void send_parse_input(struct mg_connection* connection, struct http_message* message) {
	const char* message_body = message->body.p;
	size_t message_body_len = message->body.len;
	unsigned int i = 0;
#ifdef DEBUG_MESSAGES
	printf("Incoming HTTP Message:\n");
#endif
	while (i < message_body_len) {
		char param_0[3];
		int param_1;
		double param_2;
		if (sscanf(message_body + i, "%2s %x %lf", param_0, &param_1, &param_2) < 3) break;
		else {
			if (strcmp(param_0, "di") == 0) {
				if (param_1 >= 0 && param_1 < DAQ_NUM_DIGITAL_INPUTS) {
					digital_inputs[param_1] = (param_2 == 0) ? 0 : 1;
#ifdef DEBUG_MESSAGES
					printf("%s %x %d\n", param_0, param_1, (param_2 == 0)?0:1);
#endif
				}
			}
			else if (strcmp(param_0, "ai") == 0) {
				if (param_1 >= 0 && param_1 < DAQ_NUM_ANALOG_INPUTS) {
					analog_inputs[param_1] = param_2;
#ifdef DEBUG_MESSAGES
					printf("%s %x %lf\n", param_0, param_1, param_2);
#endif
				}
			}
			while (i < message_body_len) if (message_body[i++] == '\n') break;
		}
	}
#ifdef DEBUG_MESSAGES
	printf("\n");
#endif
	mg_printf(connection, "HTTP/1.0 200 OK\r\nContent-Length: 0\r\nCache-Control: no-store\r\n"
		"Content-Type: text/plain\r\n\r\n");
	connection->flags |= MG_F_SEND_AND_CLOSE;
}

void send_bad_session(struct mg_connection* connection) {
	char* buf = "incorrect session id";
	mg_printf(connection, "HTTP/1.0 401 UNAUTHORIZED\r\nContent-Length: %d\r\nCache-Control: no-store\r\n"
		"Content-Type: text/plain\r\n\r\n%s",
		(int)strlen(buf), buf);
	connection->flags |= MG_F_SEND_AND_CLOSE;
}

void send_http(struct mg_connection* connection, const char* mime_type, const char* buf) {
	mg_printf(connection, "HTTP/1.0 200 OK\r\nContent-Length: %d\r\nCache-Control: no-store\r\n"
		"Content-Type: %s\r\n\r\n%s",
		(int)strlen(buf), mime_type, buf);
	connection->flags |= MG_F_SEND_AND_CLOSE;
}

static void event_handler(struct mg_connection* connection, int eventCode, void *eventData) {
	struct http_message *http_message = (struct http_message *) eventData;
	static struct mg_serve_http_opts http_server_opts;
	int correct_session;
	switch (eventCode) {
	case MG_EV_HTTP_REQUEST:
		correct_session = (mg_vcmp(&http_message->query_string, session_id) == 0);
		if (mg_vcmp(&http_message->uri, "/api/in") == 0) {
			if (mg_vcmp(&http_message->method, "POST") == 0) {
				if (correct_session) {
					send_parse_input(connection, http_message);
				}
				else {
					send_bad_session(connection);
				}
			}
			else {
				send_forbidden(connection);
			}
		}
		else if (mg_vcmp(&http_message->uri, "/api/out") == 0) {
			if (correct_session) {
				send_outputs(connection, 0);
				connection->user_data = eventData;
				connection->flags |= MG_F_USER_1;
			}
			else {
				send_bad_session(connection);
			}
		}
		else if (mg_vcmp(&http_message->uri, "/api/sync") == 0) {
			if (correct_session) {
				send_outputs(connection, 1);
			}
			else {
				send_bad_session(connection);
			}
		}
		else if (mg_vcmp(&http_message->uri, "/api/close") == 0) {
			if (correct_session) {
				send_http(connection, "text/plain", ".");
				printf("recived close signal\n");
				user_has_quit = 1;
			}
			else {
				send_bad_session(connection);
			}
		}
		else if (mg_vcmp(&http_message->uri, "/index") == 0) {
			char* buf = "hello";
			mg_printf(connection, "HTTP/1.0 200 OK\r\nContent-Length: %d\r\nCache-Control: no-store\r\n"
				"Content-Type: text/plain\r\n\r\n%s",
				(int)strlen(buf), buf);
			connection->flags |= MG_F_SEND_AND_CLOSE;
		}
		else if (mg_vcmp(&http_message->uri, "/lib/jquery") == 0) {
			send_http(connection, "text/javascript", jquery_js);
		}
		else if (mg_vcmp(&http_message->uri, "/simulator_gui") == 0) {
			send_http(connection, "text/html", simulator_gui_html);
		}
		else {
			//mg_serve_http(connection, http_message, http_server_opts);
			send_forbidden(connection);
		}
		break;
	case MG_EV_POLL:
		if (connection->flags & MG_F_USER_1) {
			send_outputs(connection, 0);
		}
		break;
	default:
		break;
	}
}

void* server_thread(void* param) {
	struct mg_mgr manager;
	struct mg_connection *connection = NULL;

	mg_mgr_init(&manager, NULL);

	int http_port = 8000;
	char http_port_string[6];

	while (1) {
		snprintf(http_port_string, 5, "%d", http_port);
		printf("Starting server on port %d.\n", http_port);
		connection = mg_bind(&manager, http_port_string, event_handler);
		if (connection == NULL) {
			printf("Failed to bind to port. Retrying on next port.\n");
			http_port++;
		}
		else break;
	}

	srand((unsigned int)time(NULL));
	snprintf(session_id, 8, "%08x", ((unsigned)rand() << 20) | ((unsigned)rand() << 10) | (unsigned)rand());

	// Set up HTTP server parameters
	mg_set_protocol_http_websocket(connection);

	printf("Attemptiing to start browser.\n");
	char command[200];
#if defined(_WIN32) || defined(_WIN64)
	snprintf(command, 199, "start  http://localhost:%d/simulator_gui?setup_num=%d^&session_id=%s", http_port, setup_num, session_id);
	system(command);
#elif defined(__APPLE__)
	snprintf(command, 199, "open \"http://localhost:%d/simulator_gui?setup_num=%d&&session_id=%s\"", http_port, setup_num, session_id);
	system(command);
#elif defined(__linux__)
	snprintf(command, 199, "nohup xdg-open \"http://localhost:%d/simulator_gui?setup_num=%d&session_id=%s\" &> /dev/null &", http_port, setup_num, session_id);
	popen(command, "w");
#endif
	printf("If your browser does not start, please go to the following URL:\n");
	printf("http://localhost:%d/simulator_gui?setup_num=%d&session_id=%s\n", http_port, setup_num, session_id);

	while (1) {
		mg_mgr_poll(&manager, 1);
	}
	mg_mgr_free(&manager);
	return param;
}

int daq_setup(int num) {
	if (!user_has_setup) {
		setup_num = num;
		user_has_setup = 1;
		mg_start_thread(&server_thread, NULL);
		return 1;
	}
	else {
		printf("Error: DAQ Simulator already setup.");
		return 0;
	}
}

int daq_continue_loop(void) {
	return !user_has_quit;
}

//Safe DAQ i/o functions. Uses same names, parameters as original simulator.
void digitalWrite(int channel_number, int val) {
	check_for_setup();
	if (channel_number >= 0 && channel_number < DAQ_NUM_DIGITAL_OUTPUTS) {
		daq_digital_write(channel_number, val);
	}
	else {
		printf("Error: Channel for digitalWrite out of bounds.\n");
	}
}

void displayWrite(int data, int position) {
	check_for_setup();
	if ((position >= 0) && (position < DAQ_NUM_DISPLAY_OUTPUTS) && (data >= 0) && (data < 256)) {
		daq_display_write(position, data);
	}
	else {
		printf("Error: Channel or value for displayWrite out of bounds.\n");
	}
}

int digitalRead(int channel_number) {
	check_for_setup();
	if (channel_number >= 0 && channel_number < DAQ_NUM_DIGITAL_INPUTS) {
		return daq_digital_read(channel_number);
	}
	else {
		return -1;
	}
}

double analogRead(int channel_number) {
	check_for_setup();
	if (channel_number >= 0 && channel_number < DAQ_NUM_DIGITAL_INPUTS) {
		return daq_analog_read(channel_number);
	}
	else {
		return 0.0;
	}
}

int continueSuperLoop(void) {
	return daq_continue_loop();
}

int setupDAQ(int setup_num) {
	return daq_setup(setup_num);
}

