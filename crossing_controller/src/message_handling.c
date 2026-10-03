/*
 * message_handling.c
 *
 *  Created on: 30 Sept 2026
 *      Author: aiti
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <sys/iomsg.h>
#include <sys/neutrino.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>

#include "message_handling.h"


int establish_connection(server_con_details_t *details)
{
	if (details->client_identifier <= _PULSE_CODE_MINAVAIL || details->client_identifier > _PULSE_CODE_MAXAVAIL)
	{
		printf("[MH: Error] Client_identifier (%d) is used as pulse code, must be greater than %d\n", details->client_identifier, _PULSE_CODE_MINAVAIL);
		return MH_EXIT_CLIENTID_ISSUE; // does this error code conflict with other error codes?
	}

	if (details->established == 1)
	{
		name_close(details->coid);
		details->established = 0;
		printf("[MH: System] Old connection closed. Will establish new connection\n");
	}

	printf("[MH: System] Trying to connect to server named: %s\n", details->sname);

	if ((details->coid = name_open(details->sname, 0)) == -1)
	{
		printf("[MH: Error] Could not connect to the server: %s\nError: %s\n", details->sname, strerror(errno));
		return errno;
	}
	else
	{
		details->established = 1;
		printf("[MH: System] Connection established to: %s\n", details->sname);
		return EXIT_SUCCESS;
	}
}

// ---------------------- Establish server ----------------------
// Returns EXIT_SUCCESS on success, otherwise returns the error code
int establish_server(server_create_details_t *details)
{
	details->attach = name_attach(NULL, details->name, 0);
	if (details->attach == NULL)
	{
		printf("[MH: Error] Could not establish server with name: %s\nError: %s\n", details->name, strerror(errno));
		return errno;
	}
	else
	{
		details->established = 1;
		printf("[MH: System] Server established with name: %s\n", details->name);
		return EXIT_SUCCESS;
	}
}

// ---------------------- Cleanup server ----------------------
// Returns EXIT_SUCCESS on success, otherwise returns the error code
int cleanup_server(server_create_details_t *details)
{
	if (details->established)
	{
		if (name_detach(details->attach, 0) == -1)
		{
			printf("[MH: Error] Failed to detach server with name: %s\nError is %s\n", details->name, strerror(errno));
			return errno;
		}
		else
		{
			details->established = 0;
			printf("[MH: System] Detached server with name: %s\n", details->name);
		}
	}

	return EXIT_SUCCESS;
}

int send_update_pulses(server_con_details_t *details, int event)
{
	if (details->established)
	{
		//int MsgSendPulse(int coid, int priority, int code, int value);
		// Priority = -1 = use calling thread's priority.
		// Should be adjusted if pulses have different urgency, the calling thread's priority causes issues for the reciever etc
		int err = MsgSendPulse_r(details->coid, -1, details->client_identifier, event);
		if (err != EOK)
		{
			err = -err; // Flip
			printf("[MH: Error] Failed to send update pulse. Error is: %s\n", strerror(err));
			return err;
		}
		else
		{
			printf("[MH: System] Successfully sent (CID: %d, E: %d) pulse to \"%s\"\n", details->client_identifier, event, details->sname);
			return err;
		}
	}
	else
	{
		printf("[MH: Error] Connection to \"%s\" is yet not established. Please establish the connection\n", details->sname);
		return SEND_PULSE_EXIT_CONNECTION_NOT_ESTABLISHED;
	}
}

// returns EOK on success, otherwise returns the error code. Also reply is filled with the server's response.
int send_message(server_con_details_t *details, msg_t *msg, reply_t *reply)
{
	// Just in case
	msg->client_identifier = details->client_identifier;

	if ((msg->type > _IO_MAX) == 0)
	{
		printf("[MH: Error] The message type must be greater than %d, current it is %d. Message not sent\n", _IO_MAX, msg->type);
		return EXIT_FAILURE;
	}

	if (MsgSend(details->coid, msg, sizeof(*msg), reply, sizeof(*reply)) == -1)
	{
		printf("[MH: Error] Failed to send message to \"%s\". Error is %s\n", details->sname, strerror(errno));
		return errno;
	}
	else
	{
		printf("[MH: System] Successfully sent message to \"%s\"\n", details->sname);
		return EOK;	
	}
}


int cleanup_connection(server_con_details_t *details)
{
	if (details->established)
	{
		if (name_close(details->coid) == -1)
		{
			printf("[MH: Error] Failed to close coid (%d) to \"%s\"\nError is %s\n", details->coid, details->sname, strerror(errno));
			return EXIT_FAILURE;
		}
		else
		{
			details->established = 0;
			printf("[MH: System] Closed coid (%d) on ""%s""\n", details->coid, details->sname);
		}
	}

	return EXIT_SUCCESS;
}


// ---------------------- Client dictionary ----------------------
// lookup client_details_t for given scoid, or NULL if not found
client_details_t *client_dict_lookup_scoid(client_dict_t *dict, int scoid)
{
	for (uint8_t i = 0; i < dict->count; i++)
	{
		if (dict->entries[i].scoid == scoid)
			return &dict->entries[i];
	}
	return NULL;
}

// lookup using client_id, or NULL if not found
client_details_t *client_dict_lookup_id(client_dict_t *dict, int client_id)
{
	for (uint8_t i = 0; i < dict->count; i++)
	{
		if (dict->entries[i].client_id == client_id)
			return &dict->entries[i];
	}
	return NULL;
}

// resets every field
static void client_details_init(client_details_t *entry, int scoid, int client_id)
{
	entry->scoid = scoid; // connection id of source from server pov
	entry->client_id = client_id;
	entry->disconnect_handler = NULL;
	entry->unblock_handler = NULL;
	entry->state = CLIENT_BASE_DETAILS_SET;
	entry->reqs.count = 0;
}

// Lookup by scoid, adding a new entry if not found
// update_if_exists: CLIENT_UPDATE_IF_EXISTS (and client_id does not match) resets an existing entry for the new client (scoid reuse)
// 					 CLIENT_KEEP_IF_EXISTS returns it untouched
// returns (ct = entry and rt = some return code for the new or existing entry), or (ct = NULL, rt = CLIENT_DICT_FULL) if the dictionary is full
client_dict_add_get_return_t client_dict_get_or_add(client_dict_t *dict, int scoid, int client_id, uint8_t update_if_exists)
{
	// scoid is unique and found in either pulse or message (msg_info) (sys and my own)
	client_details_t *entry = client_dict_lookup_scoid(dict, scoid);
	client_dict_add_get_return_t r = {.ct = entry, .rt = -1};
	if (entry != NULL)
	{
		// if client_id changed, then erase all prior reqs
		// feels like im taking this func out of scope too much. its become a everything func... so for nothing
		if (update_if_exists == CLIENT_UPDATE_IF_EXISTS && entry->client_id != client_id)
		{
			printf("[MH: System] Client (CID: %d) already exists in dict, updating client details... removing all reqs...\n", entry->client_id);
			close_all_reqs(entry);
			client_details_init(entry, scoid, client_id);
			r.rt = CLIENT_KEPT_UPDATED;
		}
		else
		{
			printf("[MH: System] Client (CID: %d) already exists in dict. Returning existing client details...\n", entry->client_id);
			r.rt = CLIENT_KEPT_RETURNED;
		}

		return r;
	}

	if (dict->count >= CLIENT_DICT_SIZE)
	{
		printf("[MH: Error] Client dictionary is full. Client (scoid: %d, CID: %d) not added\n", scoid, client_id);
		r.rt = CLIENT_DICT_FULL;
		r.ct = NULL;
		return r;
	}

	printf("[MH: System] Client (scoid: %d, CID: %d) added to client dict.\n", scoid, client_id);
	entry = &dict->entries[dict->count++];
	client_details_init(entry, scoid, client_id);

	r.ct = entry;
	r.rt = CLIENT_NEW_ENTRY;
	return r;
}

// Fails all pending reqs, optionally detaches the scoid (detach= CLIENT_DETACH / CLIENT_NO_DETACH),
// then removes the entry by moving last entry into its slot, so pointers to the last entry become invalid
// Returns EXIT_SUCCESS on success, EXIT_FAILURE if scoid was not found
int client_dict_remove(client_dict_t *dict, int scoid, uint8_t detach)
{
	// get the entry
	client_details_t *entry = client_dict_lookup_scoid(dict, scoid);
	if (entry == NULL)
	{
		printf("[MH: System] Failed to remove client (scoid: %d) as it does not exist in the client dict provided.\n", scoid);
		return EXIT_FAILURE;
	}

	// close all reqs in the last entry. place it here? or in disconnect_handler?
	// need to deal with errors
	printf("[MH: System] Removing all reqs for client (scoid: %d)...\n", entry->scoid);
	close_all_reqs(entry);
	if (detach == CLIENT_DETACH)
	{
		printf("[MH: System] Detaching client (scoid: %d)...\n", entry->scoid);
		if (ConnectDetach(entry->scoid) == -1)
		{
			printf("[MH: Warning] Failed to detach client (scoid: %d)... Error is %s\n", entry->scoid, strerror(errno));
		}
	}
	else
	{
		printf("[MH: System] Reqs removed, but client (scoid: %d) not detached\n", entry->scoid);
	}
	
	printf("[MH: System] Removing client (scoid: %d) from the provided client dict.\n", entry->scoid);
	// replace the entry with the last one in the dict. so now last entry can be replaced.
	*entry = dict->entries[--dict->count];
	return EXIT_SUCCESS;
}

// ---------------------- Client requests ----------------------
// Unblocks the client waiting on rcvid with EAGAIN. Errors are only logged.
static void fail_req(client_details_t *ct, int rcvid)
{
	if (MsgError(rcvid, EAGAIN) == -1)
	{
		if (errno == ESRCH)
		{
			// client has already gone, nothing is blocked on it
			printf("[MH: Warning] Failed to close req (rcvid %d) for client (scoid: %d, CID: %d) by MsgError as rcvid doesn't exist\n", rcvid, ct->scoid, ct->client_id);
		}
		else
		{
			printf("[MH: Error] Failed to close req (rcvid %d) for client (scoid: %d, CID: %d) by MsgError. Error is %s\nCould lead to starvation for the client\n", rcvid, ct->scoid, ct->client_id, strerror(errno));
		}
	}
}

// adds req r to the client's pending requests
// If full and replace_existing == REQ_ADD_REPLACE, the first replaceable request is failed with EAGAIN and overwritten
// Returns EXIT_SUCCESS on success, and returns REQ_BUFF_FULL if there was no room
int reqs_add(client_details_t *ct, uint8_t replace_existing, req_t r)
{
	req_array_t *reqs = &ct->reqs;

	if (reqs->count < REQ_BUFFER_SIZE)
	{
		// does this copy properly?
		reqs->entries[reqs->count++] = r;
		return EXIT_SUCCESS;
	}

	if (replace_existing == REQ_ADD_REPLACE)
	{
		for (uint8_t i = 0; i < reqs->count; i++)
		{
			if (reqs->entries[i].replaceable == REQ_REPLACEABLE)
			{
				fail_req(ct, reqs->entries[i].rcvid);
				reqs->entries[i] = r;

				// maybe it should say it replaced something? idk
				return EXIT_SUCCESS;
			}
		}
	}

	return REQ_BUFF_FULL;
}

// Removes the req (identified by rcvid) if its found in ct->reqs. If found returns EXIT_SUCCESS. If not found returns EXIT_FAILURE
int reqs_remove(client_details_t *ct, int rcvid)
{
	req_array_t *reqs = &ct->reqs;
	for (int i = 0; i < reqs->count; i++)
	{
		if (reqs->entries[i].rcvid == rcvid)
		{
			// like in client_dict, fill this space with the last entry and decrement.
			// shallow copy is fine here?
			reqs->entries[i] = reqs->entries[--reqs->count];
			return EXIT_SUCCESS;
		}
	}
	return EXIT_FAILURE;
}

// closes all reqs for the client and empties its req array. Returns EXIT_SUCCESS on func exit.
int close_all_reqs(client_details_t *ct)
{
	req_array_t *reqs = &ct->reqs;

	for (uint8_t i = 0; i < reqs->count; i++)
	{
		fail_req(ct, reqs->entries[i].rcvid);

		// if i have work for that rcvid. i should clean that up too
	}
	reqs->count = 0;
	return EXIT_SUCCESS;
}

// ----------------------- Add and Read from log buffer ----------------------- // 

// If failure occurs, then the data is dropped. so this should not be used for critical message passing
int add_to_log_buffer(log_buffer_t *buff, uint32_t data)
{
	// Lets keep the queue up to date. If full, skip over the next read and replace thereforth.
	int rc = pthread_mutex_lock(&buff->mutex);
	if (rc == EOK)
	{
		if (buff->count == (LOG_BUFFER_SIZE - 1)) // If only one item left before circle around
		{
			// nextRead points to oldest data. Move it one ahead.
			buff->nextRead = (buff->nextRead + 1) % LOG_BUFFER_SIZE;
			buff->count--;  // Decrement since we skipped the oldest data
		}

		// Since nextRead was moved one ahead. nextWrite will replace that skipped old element.
		buff->buffer[buff->nextWrite] = data;
		buff->nextWrite = (buff->nextWrite + 1) % LOG_BUFFER_SIZE;
		buff->count++; // Increment.

		pthread_cond_signal(&buff->cond);

		pthread_mutex_unlock(&buff->mutex);
		return EXIT_SUCCESS;
	}
	else
	{
		printf("[MH: Error] Up to date add to log buffer failed due to mutex lock: %s\nThe status update will be lost\n", strerror(rc));
		return EXIT_FAILURE;
	}
}

// blocks until data is available, returns the return code (EOK on success)
int read_from_log_buffer(log_buffer_t *buff, uint32_t *data)
{
	int rc = pthread_mutex_lock(&buff->mutex);
	if (rc == EOK)
	{
		while (buff->count == 0)
			pthread_cond_wait(&buff->cond, &buff->mutex);

		*data = buff->buffer[buff->nextRead];
		buff->nextRead = (buff->nextRead + 1) % LOG_BUFFER_SIZE;
		buff->count--;

		pthread_mutex_unlock(&buff->mutex);
	}

	return rc;
}
