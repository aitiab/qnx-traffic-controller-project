#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>

#include "message_handling.h"
#include "state_controller.h"
#include "message_controller.h"


events_t ev = {.events = {0}, .mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER};

int main(void) {
	errno = EOK; // do i need this?


	pthread_t crossing_server_tid, state_transitioner_tid;
	int rc;

	// Crossing server: admits the train controller and handles its requests
	rc = pthread_create(&crossing_server_tid, NULL, server_crossing_controller, (void *)(&ev));
	if (rc != EOK)
	{
		printf("[Error] Failed to create thread for server_crossing_controller: %s\nExiting with FAIL\n", strerror(rc));
		return EXIT_FAILURE;
	}

	// Self connection, used by the state machine to pulse the server so it re-runs its req processing
	rc = establish_connection(&self_con_details);
	if (rc != EXIT_SUCCESS)
	{
		printf("[Warning] Failed to connect to own crossing server: %s\nReqs will only be processed when a message arrives.\n", strerror(rc));
	}
	// should i do admittance?

	rc = pthread_create(&state_transitioner_tid, NULL, state_transitioner, (void *)(&ev));
	if (rc != EOK)
	{
		printf("[Error] Failed to create thread for state_transitioner: %s\nExiting with FAIL\n", strerror(rc));
		return EXIT_FAILURE;
	}

	// server_crossing_controller only returns if establish_server failed
	void *crossing_server_status = NULL;
	pthread_join(crossing_server_tid, &crossing_server_status);

	return ((intptr_t)crossing_server_status == EXIT_FAILURE) ? EXIT_FAILURE : EXIT_SUCCESS;
}
