#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#include "hardware.h"

// Crossing gates thread
// arg is crossing_gates_controller_data_t (gates + shared events_t)
// Waits for the state machine to request a gate change, applies it, then reports back via ev (pool)
void *crossing_gates_controller(void *arg)
{
	crossing_gates_controller_data_t *_d = (crossing_gates_controller_data_t *)arg;
	events_t *ev = _d->ev;
	crossing_gates_t *gates = _d->gates;

	while(1)
	{
		// avoid printf in mutex
		uint8_t gates_state = GATES_NOT_SET;
		uint8_t change = 0;
		if (pthread_mutex_lock(&gates->mutex) == EOK)
		{
			// Wait until a change of state is requested by the crossing state machine
			while (gates->next_state == GATES_NOT_SET)
			{
				pthread_cond_wait(&gates->cond, &gates->mutex);
			}

			gates->cur_state = gates->next_state;
			gates->next_state = GATES_NOT_SET;
			gates_state = gates->cur_state;
			if (gates->next_state != gates->cur_state)
				change = 1;
			pthread_mutex_unlock(&gates->mutex);
		}

		if (change  == 1)
		{
			printf("[HW] Crossing gates state changed to %s\n", (gates_state == GATES_LOWER) ? "GATES_LOWER" : "GATES_RAISE");
			//sleep(1); // simulate the time it takes to lower or raise the gates
		}

		if (pthread_mutex_lock(&ev->mutex) == EOK)
		{
			// should it check if gate came down? or was already down?
			ev->events[EV_GATE_DOWN] = (gates_state == GATES_LOWER) ? EV_GATE_DOWN_STATE_DOWN : 0;
			ev->events[EV_GATE_RAISED] = (gates_state == GATES_RAISE) ? EV_GATE_RAISED_STATE_RAISED : 0;
			pthread_cond_signal(&ev->cond);
			pthread_mutex_unlock(&ev->mutex);
		}
	}

	return (void *)EXIT_FAILURE;
}

int activate_flashers(void)
{
	printf("[HW] Flashers activated.\n");
	return EXIT_SUCCESS;
}

int deactivate_flashers(void)
{
	printf("[HW] Flashers deactivated.\n");
	return EXIT_SUCCESS;
}

/*
	Sets the next desired state of gates to next_state, then signals the crossing gates
	to process the request.

	next_state should be either GATES_LOWER or GATES_RAISE. Any other value will be ignored.

	EXIT FAILURE if it fails to get the mutex for the crossing_gates_t 
	If successful, the crossing_gates_t cur_state will be set to Gates lower

	Returns EXIT_SUCCESS if successful, otherwise returns EXIT_FAILURE
*/
int gates_req(crossing_gates_t *gates, uint8_t next_state)
{
	if (next_state != GATES_LOWER && next_state != GATES_RAISE)
	{
		printf("[HW: Error] Invalid gates state requested. Request ignored.\n");
		return EXIT_FAILURE;
	}

	if (pthread_mutex_lock(&gates->mutex) == EOK)
	{
		gates->next_state = next_state;
		pthread_cond_signal(&gates->cond);
		pthread_mutex_unlock(&gates->mutex);
	}
	else
	{
		printf("[HW: Error] Failed to get mutex for crossing gates. Gates state change request failed.\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
