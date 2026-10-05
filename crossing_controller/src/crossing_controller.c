#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <semaphore.h>
#include <unistd.h>

#include "message_handling.h"
#include "state_controller.h"
#include "message_controller.h"


events_t ev = {.events = {0}, .mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER};

void master_move_to_X1_FAULT(void)
{
	// Moving to X1_FAULT state. only called before the state transitioner is running.
	// Fault event is set so the server replies STOP_TRAIN to any train requests.
	if (pthread_mutex_lock(&ev.mutex) == EOK)
	{
		ev.events[EV_X1_FAULT] = EV_X1_FAULT_STATE_ON;
		pthread_cond_broadcast(&ev.cond);
		pthread_mutex_unlock(&ev.mutex);
	}
	// deal with when it fails?
	next_state = X1_FAULT;
	state_transition_message();
	cur_state = next_state;
	activate_flashers();
	gates_down();
	// pretty bad...
	while (1)
		sleep(5);
}
int main(void) {
	errno = EOK; // do i need this?

	sem_t server_established_indicator;
	sem_init(&server_established_indicator, 0, 0);

	pthread_t crossing_server_tid, state_transitioner_tid, central_messenger_tid;
	int rc;

	rc = pthread_create(&central_messenger_tid, NULL, subserver_central_messenger, NULL);
	if (rc != EOK)
	{
		printf("[Main: Error] Failed to create thread for subserver_central_messenger: %s\n", strerror(rc));
		// Fail silently
	}

	server_crossing_controller_data _d = {.ev = &ev, .sem = &server_established_indicator};
	// Crossing server: admits the train controller and handles its requests
	void *crossing_server_status = (void *)NULL;
	rc = pthread_create(&crossing_server_tid, NULL, server_crossing_controller, (void *)(&_d));
	if (rc != EOK)
	{
		printf("[Main: Error] Failed to create thread for server_crossing_controller: %s\nMoving to X1_FAULT.\n", strerror(rc));
		master_move_to_X1_FAULT();
		return EXIT_FAILURE; // ?? or a infinite loop
	}

	// Wait until the server has been established before attempting to connect to it
	while (sem_wait(&server_established_indicator) == -1 && errno == EINTR);
	if (crossing_server_details.status == STATUS_FAILED)
	{
		printf("[Main: Error] Crossing Server failed to establish, moving state to X1_FAULT\n");
		pthread_join(crossing_server_tid, &crossing_server_status);
		master_move_to_X1_FAULT();
		return EXIT_FAILURE;
	}
	else
	{
		// Self connection, used by the state machine to pulse the server so it re-runs its req processing
		rc = establish_connection(&self_con_details);
		if (rc != EXIT_SUCCESS)
		{
			// Without self wake pulses, APPROACH_NOTIFY reqs cant progress past RUNNING_S2, so treat as fatal
			printf("[Main: Error] Failed to connect to own crossing server: %s\nMoving to X1_FAULT.\n", strerror(rc));
			master_move_to_X1_FAULT();
			return EXIT_FAILURE;
		}
		// should i do admittance?
	}
	

	rc = pthread_create(&state_transitioner_tid, NULL, state_transitioner, (void *)(&ev));
	if (rc != EOK)
	{
		printf("[Main: Error] Failed to create thread for state_transitioner: %s\nExiting with FAIL\n", strerror(rc));
		//pthread_join()
		master_move_to_X1_FAULT();
		return EXIT_FAILURE;
	}

	// server_crossing_controller only returns if establish_server failed
	pthread_join(crossing_server_tid, &crossing_server_status);

	return ((intptr_t)crossing_server_status == EXIT_FAILURE) ? EXIT_FAILURE : EXIT_SUCCESS;
}
