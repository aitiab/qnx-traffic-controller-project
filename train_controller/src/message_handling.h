/*
 * message_handling.h
 *
 *  Created on: 30 Sept 2026
 *      Author: aiti
 */

#ifndef SRC_MESSAGE_HANDLING_H_
#define SRC_MESSAGE_HANDLING_H_

#include <stdint.h>
#include <pthread.h>

#define MH_EXIT_CLIENTID_ISSUE 2
#define SEND_PULSE_EXIT_CONNECTION_NOT_ESTABLISHED 3

//  Status flags
#define THREAD_DID_NOT_START 2
#define STATUS_FAILED 3
#define STATUS_RUNNING 1

typedef struct {
	uint8_t connection_established;
	int server_coid; // as returned by name_open
	uint8_t client_identifier; // Used as the code in pulses
	char *sname;
	uint8_t status;
} server_con_details_t;

typedef struct {
	uint16_t type;
	uint8_t client_identifier;
	uint16_t data;
} msg_t;

typedef struct
{
	uint16_t data;
} reply_t;

// --------------------------- START Log Buffer Definition ------------------------------- //
// Up to date log buffer: when full, the oldest entry is overwritten
#define LOG_BUFFER_SIZE 10
typedef struct {
	pthread_mutex_t mutex;
	pthread_cond_t 	cond;
	uint8_t 		count;
	uint32_t 		buffer[LOG_BUFFER_SIZE];
	uint8_t 		nextRead;
	uint8_t 		nextWrite;
} log_buffer_t;
// --------------------------- END Log Buffer Definition ------------------------------- //

int establish_connection(server_con_details_t *details);
int send_update_pulses(server_con_details_t *details, int event);

int add_to_log_buffer(log_buffer_t *buff, uint32_t data);
int read_from_log_buffer(log_buffer_t *buff, uint32_t *data);



#endif /* SRC_MESSAGE_HANDLING_H_ */
