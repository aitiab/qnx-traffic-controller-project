/*
 * message_handing.c
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
		return MH_EXIT_CLIENTID_ISSUE;
	}

	if (details->connection_established == 1)
	{
		name_close(details->server_coid);
		details->connection_established = 0;
		printf("[MH: System] Old connection closed. Will establish new connection\n");
	}

	printf("[MH: System] Trying to connect to server named: %s\n", details->sname);

	if ((details->server_coid = name_open(details->sname, 0)) == -1)
	{
		printf("[MH: Error] Could not connect to the server: %s\n", details->sname);
		return EXIT_FAILURE;
	}
	else
	{
		details->connection_established = 1;
		printf("[MH: System] Connection established to: %s\n", details->sname);
		return EXIT_SUCCESS;
	}
}

int send_update_pulses(server_con_details_t *details, int event)
{
	if (details->connection_established)
	{
		//int MsgSendPulse(int coid, int priority, int code, int value);
		// Priority = -1 = use calling thread's priority.
		// Should be adjusted if pulses have different urgency, the calling thread's priority causes issues for the reciever etc
		int err = MsgSendPulse_r(details->server_coid, -1, details->client_identifier, event);
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

int send_message(server_con_details_t *details, msg_t *msg, reply_t *reply)
{
	// Just in case
	msg->client_identifier = details->client_identifier;

	if ((msg->type > _IO_MAX) == 0)
	{
		printf("[MH: Error] The message type must be greater than %d, current it is %d. Message not sent\n", _IO_MAX, msg->type);
		return EXIT_FAILURE;
	}

	if (MsgSend(details->server_coid, sizeof(*msg), reply, sizeof(*reply)) == -1)
	{

	}
}


int cleanup(server_con_details_t *details)
{
	if (details->connection_established)
	{
		if (name_close(details->server_coid) == -1)
		{
			printf("[MH: Error] Failed to close coid (%d) to \"%s\"\nError is %s\n", details->server_coid, details->sname, strerror(errno));
			return EXIT_FAILURE;
		}
		else
		{
			details->connection_established = 0;
			printf("[MH: System] Closed coid (%d) on ""%s""\n", details->server_coid, details->sname);
		}
	}

	return EXIT_SUCCESS;
}


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
		printf("[Error] Up to date add to log buffer failed due to mutex lock: %s\nThe status update will be lost\n", strerror(rc));
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
