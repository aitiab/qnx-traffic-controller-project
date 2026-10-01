#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include "message_handling.h"
#include "state_controller.h"
#include "input.h"
#include "message_controller.h"


int main(void) {
	errno = EOK;

	pthread_t terminal_in_tid, central_massenger_tid;
	int rc;

	rc = pthread_create(&central_massenger_tid, NULL, subserver_central_messenger, NULL);
	if (rc != EOK)
	{
		printf("[Error] Failed to create thread for subserver_central_messenger: %s\n", strerror(rc));
		// Fail silently
	}

	// higher priority than usual needed
	rc = pthread_create(&terminal_in_tid, NULL, terminal_in, NULL);
	if (rc != EOK)
	{
		printf("[Error] Failed to create thread for terminal input reader: %s\n", strerror(rc));
		// What happens when the mutex is not accessed. It cant go on. so what happens???
		printf("[Error] Failed to get mutex on input_obj to set the status. Exiting with FAIL\n");
		cur_state = SYS_FAIL;
		return EXIT_FAILURE;
	}



	events_t ev = DEFAULT;
	uint8_t stop = 0;
	int message = -1;
	while(!stop)
	{
		switch (cur_state){
		case SYS_FAIL:
			stop = 1;
			if (message != -1)
				add_to_log_buffer(&pulses_to_central, (uint32_t)message);
			break;
		case NRML:
			readInput(&ev);
			message = state_controller_events(&ev);
			if (message != -1)
				add_to_log_buffer(&pulses_to_central, (uint32_t)message);
			break;
		case APRCH:
			readInput(&ev);
			message = state_controller_events(&ev);
			if (message != -1)
				add_to_log_buffer(&pulses_to_central, (uint32_t)message);
			break;
		case EM_B_CROSS:
			readInput(&ev);
			message = state_controller_events(&ev);
			if (message != -1)
				add_to_log_buffer(&pulses_to_central, (uint32_t)message);
			break;
		case CROSS:
			readInput(&ev);
			message = state_controller_events(&ev);
			if (message != -1)
				add_to_log_buffer(&pulses_to_central, (uint32_t)message);
			break;
		case EM_A_CROSS:
			readInput(&ev);
			message = state_controller_events(&ev);
			if (message != -1)
				add_to_log_buffer(&pulses_to_central, (uint32_t)message);
			break;
		default:
			printf("[Warning] Train controller is on an undefined state %d. Changing state back to NRML.\n", cur_state);
			next_state = NRML;
		}

		cur_state = next_state;


	}

	// Need to kill the threads...
	// Need to worry about letting central messenger finish

	pthread_join(central_massenger_tid, NULL);
	pthread_join(terminal_in_tid, NULL);

	return EXIT_SUCCESS;
}


// Message to the train crossing


