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
#include <sys/dispatch.h>

#define MH_EXIT_CLIENTID_ISSUE 2
#define SEND_PULSE_EXIT_CONNECTION_NOT_ESTABLISHED 3

//  Status flags
#define THREAD_DID_NOT_START 2
#define STATUS_FAILED 3
#define STATUS_RUNNING 1

typedef struct {
	uint8_t established;
	int coid; // as returned by name_open
	uint8_t client_identifier; // Used as the code in pulses
	char *sname;
	uint8_t status;
} server_con_details_t;


typedef struct {
	char *name;
	uint8_t established;
	name_attach_t *attach; // as returned by name_attach
	uint8_t status;
} server_create_details_t;


typedef struct {
	uint16_t type;
	uint8_t client_identifier;
	uint16_t data;
} msg_t;

typedef struct
{
	uint16_t data;
} reply_t;

typedef struct {
	int scoid; // as returned by MsgReceive
	uint8_t client_id; // Used as the code in pulses or the client identifier in messages
	uint8_t state;
} client_details_t;

// --------------------------- START Client Dictionary Definition ------------------------------- //
// Fixed size dictionary keyed by scoid. Linear search. Only one thread should access. no synchronisatoin used.
#define CLIENT_DICT_SIZE 16
typedef struct {
	client_details_t entries[CLIENT_DICT_SIZE];
	uint8_t 		 count;
} client_dict_t;
// --------------------------- END Client Dictionary Definition ------------------------------- //

typedef union {
	msg_t 			msg;
	struct _pulse pulse;
} recv_t;

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
int cleanup_connection(server_con_details_t *details);

int establish_server(server_create_details_t *details);
int send_message(server_con_details_t *details, msg_t *msg, reply_t *reply);
int cleanup_server(server_create_details_t *details);

client_details_t *client_dict_find(client_dict_t *dict, int scoid);
client_details_t *client_dict_find_by_id(client_dict_t *dict, uint8_t client_id);
client_details_t *client_dict_add(client_dict_t *dict, int scoid, uint8_t client_id);
int client_dict_remove(client_dict_t *dict, int scoid);


int add_to_log_buffer(log_buffer_t *buff, uint32_t data);
int read_from_log_buffer(log_buffer_t *buff, uint32_t *data);



#endif /* SRC_MESSAGE_HANDLING_H_ */
