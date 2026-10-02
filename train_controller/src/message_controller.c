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

void *server_crossing_controller(void *arg)
{
	
	// Need to worry about if establish_server fails, it should send the train in SYS_FAULT state.
	int rc = establish_server(&crossing_server_details);
	if (rc == EXIT_FAILURE)
	{
		crossing_server_details.status = STATUS_FAILED;
		printf("[Error] Failed to establish server for the crossing controller.\server_crossing_controller thread is exiting\n");
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
			continue;
		}

		if (rcvid == 0)
		{
			// Pulse received
			printf("[CrossServer: System] Pulse received. Code is %d, Value is %d\n", recv.pulse.code, recv.pulse.value);
			
			client_details_t *ct = client_dict_lookup_scoid(&client_dict, recv.pulse.scoid);
			
			// // is this valid? shouldnt it depend on the kernal limit? 
			// if (ct == NULL)
			// {
			// 	printf("[CrossServer: Error] Clients dict is full. Will drop this pulse. Might be fatal\n");
			// }
			
			client_handler_data_t *ct_d = {.ct = ct, .data = NULL};

			switch (recv.pulse.code)
			{
				// Find someway to connect clientID with scoid.
				// Use value to let client decide cancel/or not?
				case _PULSE_CODE_DISCONNECT:
					printf("[CrossServer: System] PULSE_CODE_DISCONNECT received from client %d\n", recv.pulse.scoid);
					// Free any state kept with client

					// If scoid was not found in our established client list (from _IO_CONNECT)
					// just let it detach. might be due to failed connection or something
					if (ct != NULL)
					{
						if (ct->disconnect_handler != NULL)
						{
							ct->disconnect_handler((void *)ct_d);
						}
						// else
						// {
						// 	// maybe a default one?
						// }

						// remove from dict and detach 
						if (client_dict_remove(&client_dict, recv.pulse.scoid, 1) == EXIT_FAILURE)
						{
							printf("[CrossServer: Warning] Attempted to remove and detach client %d, but it was not found in client dict\n", ct->client_id);
						}
					}
					else
					{
						ConnectDetatch(recv.pulse.scoid);
					}
					
					break;
				case _PULSE_CODE_UNBLOCK:
					printf("[CrossServer: System] PULSE_CODE_UNBLOCK received from client %d\n", recv.pulse.scoid);
					
					int unblocked_rcvid = recv.pulse.value.sival_int;
					// just random idek
					ct_d->data = &unblocked_rcvid;
					if (ct->unblock_handler != NULL)
					{
						ct->unblock_handler((void *)ct_d);
					}
					// else
					// {
					// 	// Default one?
					// }

					// recv.pulse.value.sival_int holds the recv
					MsgError(unblocked_rcvid, EINTR);
					break;
				// My codes. should use it to get the clientID and set the disconnect and unblock handler.
				default:
					printf("[CrossServer: Warning] Received unknown pulse code %d from central controller. Value is %d\n", recv.pulse.code, recv.pulse.value);
					break;
			}

			continue; // go back to the top of the while loop
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

			if (recv.msg.type > _IO_BASE && recv.msg.type <= _IO_MAX)
			{
				MsgError(rcvid, ENOSYS);
				printf("\n[CrossServer: System] Server recieved IO message and rejected it (ENOSYS)\n");
				continue
			}
			
			// need to add my own admitter. (separate form the IO_CONNECT>)
			
			// should there be more checks here?
			// client add or get will reset the client_details_t if update_if_exists = 1. includes cancelling all reqs
			client_details_t *ct = client_dict_add_or_get(&client_dict, info.scoid, recv.msg.client_identifier, 1);
			if (ct->state == CLIENT_BASE_DETAILS_SET)
			{
				client_handler_initaliser(ct);
			}

			if (ct->client_id == CROSSING_CONTROLLER_ID)
			{

			}
			reqs_add(&ct, REQ_ADD_REPLACE)
			ct->reqs
			

			reply_t reply = {
				.data = 0 // Just a dummy reply for now.
			};
			if (MsgReply(rcvid, EOK, &reply, sizeof(reply)) == -1)
			{
				printf("[CrossServer: Error] Failed to reply to the message from central controller. Error is %s\n", strerror(errno));
			}
		}
	}
}

static void _disconnect_handler(void *data)
{
	//
}

static void _unblock_handler(void *data)
{

}

void client_handler_initaliser(client_details_t *ct)
{
	switch (ct->client_id)
	{
	case CROSSING_CONTROLLER_CLIENT_ID:
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
