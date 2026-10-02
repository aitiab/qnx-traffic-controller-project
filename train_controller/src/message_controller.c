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

server_create_details_t central_server_details = {
	.name = TRAIN_CONTROLLER_ATTACH_POINT,
	.established = 0,
	.attach = NULL,
	.status = STATUS_RUNNING
};

void *server_train_controller(void *arg)
{
	
	// Need to worry about if establish_server fails, it should send the train in SYS_FAULT state.
	int rc = establish_server(&central_server_details);
	if (rc == EXIT_FAILURE)
	{
		central_server_details.status = STATUS_FAILED;
		printf("[Error] Failed to establish server for the train controller.\nserver_train_controller thread is exiting\n");
		return (void *)EXIT_FAILURE;
	}

	recv_t recv;
	while (1)
	{
		// Do i need to use the msg_info?
		int rcvid = MsgReceive(central_server_details.attach->chid, &recv, sizeof(recv), NULL);
		if (rcvid == -1)
		{
			printf("[TrainServer: Warning] Failed to receive message. Error is %s\n", strerror(errno));
			continue;
		}

		if (rcvid == 0)
		{
			// Pulse received
			printf("[TrainServer: System] Pulse received. Code is %d, Value is %d\n", recv.pulse.code, recv.pulse.value);
		
			switch (recv.pulse.code)
			{
				// Find someway to connect clientID with scoid.
				// Use value to let client decide cancel/or not?
				
				case _PULSE_CODE_DISCONNECT:
					printf("[TrainServer: System] PULSE_CODE_DISCONNECT received from client %d\n", recv.pulse.scoid);
					// Free any state kept with client
					client_disconnect_handler(recv.pulse.scoid);
					ConnectDetach(recv.pulse.scoid);
					break;
				case _PULSE_CODE_UNBLOCK:
					printf("[TrainServer: System] PULSE_CODE_UNBLOCK received from client %d\n", recv.pulse.scoid);
					break;
				
				default:
					printf("[TrainServer: Warning] Received unknown pulse code %d from central controller. Value is %d\n", recv.pulse.code, recv.pulse.value);
					break;
			}
		}


		if (rcvid > 0)
		{
			// Message received
			printf("[TrainServer: System] Received message from central controller. Type is %d, Data is %d\n", recv.msg.type, recv.msg.data);
			reply_t reply = {
				.data = 0 // Just a dummy reply for now.
			};
			if (MsgReply(rcvid, EOK, &reply, sizeof(reply)) == -1)
			{
				printf("[TrainServer: Error] Failed to reply to the message from central controller. Error is %s\n", strerror(errno));
			}
		}
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
