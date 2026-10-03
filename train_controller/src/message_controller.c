#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>

#include "message_controller.h"


log_buffer_t pulses_to_central = {
	.mutex 	= PTHREAD_MUTEX_INITIALIZER,
	.cond 	= PTHREAD_COND_INITIALIZER
};

// Connection details to central controller
server_con_details_t central_con_details = {
	.established = 0,
	.coid = -1,
	.client_identifier = 101, // need to discuss with the central controller
	.sname = QNET_CENTRAL_CONTROLLER_ATTACH_POINT, // What's the consequence of this?
	.status = STATUS_RUNNING
};

// just preping here. move to crossing controller's message controller later
server_create_details_t crossing_server_details = {
	.name = CROSSING_SERVER_ATTACH_POINT,
	.established = 0,
	.attach = NULL,
	.status = STATUS_RUNNING
};


static void _disconnect_handler(void *data);
static void _unblock_handler(void *data);
static void client_handler_initaliser(client_details_t *ct);
static int _my_message_handler(client_dict_t *client_dict, struct _msg_info *info, int rcvid, recv_t *recv);
static int _my_req_process();


void *server_crossing_controller(void *arg)
{
	
	// Need to worry about if establish_server fails, it should send the train in SYS_FAULT state.
	// establish_server returns EXIT_SUCCESS is succeed, otherwise passes down the errno
	int rc = establish_server(&crossing_server_details);
	if (rc != EXIT_SUCCESS)
	{
		crossing_server_details.status = STATUS_FAILED;
		printf("[Error] Failed to establish server for the crossing controller...\n\"server_crossing_controller\" thread is exiting.\n");
		return (void *)EXIT_FAILURE;
	}

	// will this {0} set both count and the inner struct?
	client_dict_t client_dict = {0};

	recv_t recv;
	while (1)
	{
		struct _msg_info info;
		int rcvid = MsgReceive(crossing_server_details.attach->chid, &recv, sizeof(recv), &info);
		if (rcvid == -1)
		{
			printf("[CrossServer: Warning] Failed to receive message. Error is %s\n", strerror(errno));
			//continue;
		}

		if (rcvid == 0)
		{
			// Pulse received
			printf("[CrossServer: System] Pulse received (scoid: %d, code: %d, value: %d)\n", recv.pulse.scoid, recv.pulse.code, recv.pulse.value.sival_int);
			
			client_details_t *ct = client_dict_lookup_scoid(&client_dict, recv.pulse.scoid);
			
			// // is this valid? shouldnt it depend on the kernal limit? 
			// if (ct == NULL)
			// {
			// 	printf("[CrossServer: Error] Clients dict is full. Will drop this pulse. Might be fatal\n");
			// }
			
			client_handler_data_t ct_d = {.ct = ct, .data = NULL};

			switch (recv.pulse.code)
			{
				// Find someway to connect clientID with scoid.
				// Use value to let client decide cancel/or not?
				case _PULSE_CODE_DISCONNECT:
					printf("[CrossServer: System] PULSE_CODE_DISCONNECT received from client %d\n", recv.pulse.scoid);
					// Free any state kept with client

					// If scoid was not found in our established client list (from ADMIT)
					// just let it detach. might be due to failed connection or something
					if (ct != NULL)
					{
						if (ct->disconnect_handler != NULL)
						{
							ct->disconnect_handler((void *)&ct_d);
						}
						// else
						// {
						// 	// maybe a default one?
						// }

						// remove from dict and detach 
						client_dict_remove(&client_dict, recv.pulse.scoid, CLIENT_DETACH);
					}
					else
					{
						ConnectDetach(recv.pulse.scoid);
					}
					break;
				case _PULSE_CODE_UNBLOCK:
					printf("[CrossServer: System] PULSE_CODE_UNBLOCK received from client %d\n", recv.pulse.scoid);
					
					int unblocked_rcvid = recv.pulse.value.sival_int;
					// just random idek
					if (ct != NULL)
					{
						ct_d.data = &unblocked_rcvid;
						if (ct->unblock_handler != NULL)
						{
							ct->unblock_handler((void *)&ct_d);
						}
						// else
						// {
						// 	// Default one?
						// }
					}
					// recv.pulse.value.sival_int holds the recv
					MsgError(unblocked_rcvid, EINTR);
					break;
				// My codes. should use it to get the clientID and set the disconnect and unblock handler.
				default:
					printf("[CrossServer: Warning] Received unknown pulse code %d. Value is %d\n", recv.pulse.code, recv.pulse.value.sival_int);
					break;
			}

			//continue; // go back to the top of the while loop
		}


		if (rcvid > 0)
		{
			// Message received
			printf("[CrossServer: System] Received message from central controller. Type is %d, Data is %d\n", recv.msg.type, recv.msg.data);
			
			if (recv.msg.type == _IO_CONNECT)
			{
				MsgReply(rcvid, EOK, NULL, 0);
				printf("\n[CrossServer: System] Server recieved _IO_CONNECT message and replyed with EOK\n");
				continue;
			}
			else if (recv.msg.type > _IO_BASE && recv.msg.type <= _IO_MAX)
			{
				MsgError(rcvid, ENOSYS);
				printf("\n[CrossServer: System] Server recieved IO message and rejected it (ENOSYS)\n");
				continue;
			}
			// need to add my own admitter. (separate form the IO_CONNECT>)
			else if (recv.msg.type == ADMITTER_CODE)
			{
				// client add or get will reset the client_details_t if update_if_exists = 1. includes cancelling all reqs
				client_dict_add_get_return_t r = client_dict_get_or_add(&client_dict, info.scoid, recv.msg.client_identifier, CLIENT_UPDATE_IF_EXISTS);
				if (r.ct == NULL)
				{
					MsgError(rcvid, EBUSY); // client_dict is full. reject connection
					printf("\n[CrossServer: System] Server recieved ADMITTER message but client dict was full... replyed with EBUSY\n");
					continue; // ?
				}
				MsgReply(rcvid, EOK, NULL, 0);
				printf("\n[CrossServer: System] Server recieved ADMITTER message and replyed with EOK\n");
				continue;
			}
			else
			{
				if (_my_message_handler(&client_dict, &info, rcvid, &recv) == EXIT_FAILURE)
				{
					// If my handlers couldnt handle the message, then no func exists to deal with it
					MsgError(rcvid, ENOSYS);
				}
			}
			
		}

		// req processing



	}
}

static int _my_req_process()
{
	
}

// returns EXIT_SUCCESS if the message was handled. EXIT_FAILURE if it wasnt (i.e. end of the func was reached)
static int _my_message_handler(client_dict_t *client_dict, struct _msg_info *info, int rcvid, recv_t *recv)
{
	// if client_id has changed (scoid reused) we believe its the same client
	// but if they disconnected it should be removed in the disconnect pulse handling. so i wonder if this is ok
	client_details_t *ct = client_dict_lookup_scoid(client_dict, info->scoid);

	if (ct == NULL)
	{
		// Maybe should send something in the reply? Some data and not just a error?
		printf("[CrossServer: Warning] Client's details could not be found in the. Returning EINVAL (request invalid, admit first)\n");
		MsgError(rcvid, EINVAL);
		// If not found in the list, then ignore this rcvid
		return EXIT_SUCCESS; // ???
	}

	// if at this point then client exists and valid

	if (ct->state == CLIENT_BASE_DETAILS_SET)
	{
		client_handler_initaliser(ct);
	}

	if (ct->client_id == TRAIN_CONTROLLER_CLIENT_ID)
	{
		if (recv->msg.type == CROSSING_NOTIFY)
		{
			req_t rq = {.rcvid = rcvid, .replaceable = REQ_NOT_REPLACEABLE, .type = recv->msg.type, .subtype = recv->msg.subtype};
			if (reqs_add(ct, REQ_ADD_REPLACE, rq) == REQ_BUFF_FULL)
			{
				reply_t rply = {.status = EAGAIN, .err_msg = "REQ_BUFF_FULL", .data = 0};
				// EOK indicates message was recieved by server, but they check .status = EGAIN and .err_msg
				if (MsgReply(rcvid, EOK, &rply, sizeof(rply)) == -1)
				{
					if (errno != ESRCH)
					{
						MsgError(rcvid, EAGAIN);
					}
				}
				printf("[CrossServer: Warning] Train sent CROSSING_NOTIFY but the request buffer was full. Returned EAGAIN.\n");
			}
			else
				printf("[CrossServer: System] Train sent CROSSING_NOTIFY, request saved.\n");
		
			return EXIT_SUCCESS;
		}
	}

	return EXIT_FAILURE;
}

static void _disconnect_handler(void *data)
{
	//
}

static void _unblock_handler(void *data)
{
	client_handler_data_t *d = (client_handler_data_t *)data;  
	// maybe should have more rules or maybe a struct for data
	reqs_remove(d->ct, *(int *)d->data);
}

static void client_handler_initaliser(client_details_t *ct)
{
	switch (ct->client_id)
	{
	case TRAIN_CONTROLLER_CLIENT_ID:
		ct->disconnect_handler = _disconnect_handler;
		ct->unblock_handler = _unblock_handler;
		ct->state = CLIENT_ALL_DETAILS_SET;
		break;
	default:
		printf("[CrossServer: Error] ClientID not recognised, could not set the disconnect and unblock handlers\n");
		break;
	}
}


// Need to handle lost updates.
void *subserver_central_messenger(void *arg)
{
	// Employee handling messages to central server asynchronously


	// What should happen if it fails to connect to central controller?
	// Timed reconnection?. Just die?
	int rc = establish_connection(&central_con_details);
	if (rc == EXIT_FAILURE || rc == MH_EXIT_CLIENTID_ISSUE)
	{
		central_con_details.status = STATUS_FAILED;
		printf("[Error] Failed to connect to the central controller.\nsubserver_central_messager thread is exiting\n");
		return (void *)EXIT_FAILURE;
	}


	int event_given = 0; // indicate if thread should sleep as there is no work
	int should_sleep = 0;
	int event = 0;
	uint32_t data = 0;

	// Is while 1 a good idea? should there be an exit strategy?
	while (1)
	{
		event_given = 0; // indicate if thread should sleep as there is no work
		should_sleep = 0;
		event = 0;

		rc = read_from_log_buffer(&pulses_to_central, &data);
		if (rc == EOK)
		{
			event = data;
			event_given = 1;
		}
		else
		{
			printf("[Warning] Failed to get pulses from pulses_to_central buffer due to mutex lock: %s\n", strerror(rc));
		}

		if (event_given)
		{
			rc = send_update_pulses(&central_con_details, event);
			if (rc != EOK)
			{
				if (rc == EAGAIN)
				{
					printf("[Warning] Central controller's kernel had insufficient resources to enqueue pulse.\nThe update message is lost");
					should_sleep = 1;
				}
				else
				{
					central_con_details.status = STATUS_FAILED;
					printf("[Error] send_update_pulses failed. The central controller messenger thread is exiting.\n");
					return (void *)EXIT_FAILURE;
				}
			}
		}

		if (should_sleep)
		{
			sleep(2); // No work to do, sleep for some seconds.
		}

	}



	return NULL;
}
