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

// Structure closely follows _pulse with type, subtype, code (clientID)
typedef struct {
	uint16_t type;
	uint16_t subtype;
	int8_t client_identifier;
	int data;
} msg_t;

#define ERR_MSG_SIZE 20
typedef struct
{
	int status;
	char err_msg[ERR_MSG_SIZE];
	int data;
} reply_t;


// --------------------------- START Client Dictionary Definitions ------------------------------- //
#define REQ_REPLACEABLE 	1
#define REQ_NOT_REPLACEABLE 0
typedef struct {
	int 		rcvid;
	uint16_t 	type;
	uint16_t 	subtype;
	uint8_t 	replaceable; // if this req can be dropped when the array is full and a new item has to be put in
} req_t;

// Pending requests per client
#define REQ_BUFFER_SIZE 5
typedef struct {
	req_t 	entries[REQ_BUFFER_SIZE];
	uint8_t count;
} req_array_t;


// Client detail flags
#define CLIENT_UNSET 0
#define CLIENT_BASE_DETAILS_SET 1
#define CLIENT_ALL_DETAILS_SET 2
#define CLIENT_BAD_STATE 3
typedef struct {
	int 			scoid; // as returned by MsgReceive
	int 			client_id; // Used as the code in pulses or the client identifier in messages. -1 = not known
	void 			(*disconnect_handler)(void *data);
	void 			(*unblock_handler)(void *data);
	int 			state; // 0 = unset, 1 = scoid, client_id set, 2 = all set, 3 = something bad.
	req_array_t 	reqs;
} client_details_t;

// struct for passing data into the disconnect and unblock handlers
typedef struct
{
	client_details_t *ct; 
	void *data;
} client_handler_data_t;

// struct for getting a simple dict.
#define CLIENT_DICT_SIZE 16
typedef struct {
	client_details_t entries[CLIENT_DICT_SIZE];
	uint8_t 		 count;
} client_dict_t;
// --------------------------- END Client Dictionary Definitions ------------------------------- //

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

#define SEND_TIMED_OUT_NOT_RECEIVED 1
#define SEND_REPLY_EINTR 2 // could be from unblock or an actual EINTR errorMsg although would be a bad design?
#define SEND_OTHER_ERROR 3
int send_message_timed(server_con_details_t *details, msg_t *msg, reply_t *reply, uint64_t ms_timeout);
#define SEND_ADMIT_TIMEOUT_MS 500
int send_admit_message(server_con_details_t *details, int SERVER_ADMITTANCE_CODE, reply_t *reply);
int cleanup_server(server_create_details_t *details);

client_details_t *client_dict_lookup_scoid(client_dict_t *dict, int scoid);
client_details_t *client_dict_lookup_id(client_dict_t *dict, int client_id);

#define CLIENT_KEPT_RETURNED 	1
#define CLIENT_KEPT_UPDATED  	2
#define CLIENT_NEW_ENTRY 		0
typedef struct
{
	client_details_t *ct;
	int8_t 		 rt; // Return type. 0 = New Entry, 1 = existing returned, 2 = existing updated returned
} client_dict_add_get_return_t;

#define CLIENT_UPDATE_IF_EXISTS 1
#define CLIENT_KEEP_IF_EXISTS 	0
#define CLIENT_DICT_FULL 		-2
client_dict_add_get_return_t client_dict_get_or_add(client_dict_t *dict, int scoid, int client_id, uint8_t update_if_exists);

#define CLIENT_DETACH 			1
#define CLIENT_NO_DETACH 		0
int client_dict_remove(client_dict_t *dict, int scoid, uint8_t detach);

#define REQ_ADD_REPLACE 		1
#define REQ_ADD_NOT_REPLACE 	0
#define REQ_BUFF_FULL 			-1
int reqs_add(client_details_t *ct, uint8_t replace_existing, req_t r);
int close_all_reqs(client_details_t *ct);
int reqs_remove (client_details_t *ct, int rcvid);

int add_to_log_buffer(log_buffer_t *buff, uint32_t data);
int read_from_log_buffer(log_buffer_t *buff, uint32_t *data);



#endif /* SRC_MESSAGE_HANDLING_H_ */
