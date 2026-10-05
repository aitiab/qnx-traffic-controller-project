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
	.client_identifier = TRAIN_CONTROLLER_CLIENT_ID, // need to discuss with the central controller
	.sname = QNET_CENTRAL_CONTROLLER_ATTACH_POINT, // What's the consequence of this?
	.status = STATUS_RUNNING
};

// Connection details to crossing controller
server_con_details_t crossing_con_details = {
	.established = 0,
	.coid = -1,
	.client_identifier = TRAIN_CONTROLLER_CLIENT_ID, // need to discuss with the central controller
	.sname = QNET_CROSSING_SERVER_ATTACH_POINT, // What's the consequence of this?
	.status = STATUS_RUNNING // should it be running on start? 
};

// Need to handle lost updates.
void *subserver_central_messenger(void *arg)
{
	// Employee handling messages to central server asynchronously


	// What should happen if it fails to connect to central controller?
	// Timed reconnection?. Just die?
	int rc = establish_connection(&central_con_details);
	if (rc != EXIT_SUCCESS)
	{
		central_con_details.status = STATUS_FAILED;
		printf("[Central: Error] Failed to connect to the central controller.\nsubserver_central_messager thread is exiting\n");
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
			printf("[Central: Warning] Failed to get pulses from pulses_to_central buffer due to mutex lock: %s\n", strerror(rc));
		}

		if (event_given)
		{
			rc = send_update_pulses(&central_con_details, event);
			if (rc != EOK)
			{
				if (rc == EAGAIN)
				{
					printf("[Central: Warning] Central controller's kernel had insufficient resources to enqueue pulse.\nThe update message is lost\n");
					should_sleep = 1;
				}
				else
				{
					central_con_details.status = STATUS_FAILED;
					printf("[Central: Error] send_update_pulses failed. The central controller messenger thread is exiting.\n");
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
