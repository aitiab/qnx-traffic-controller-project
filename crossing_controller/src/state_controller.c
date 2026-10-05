#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#include "state_controller.h"
#include "message_controller.h"


// --------------------------- START Crossing States Definition ------------------------------- //
const char *state_to_string[] =
{
	[IDLE] 				= "IDLE",
	[TRAIN_APPROACHING] = "TRAIN_APPROACHING",
	[WARNING_ACTIVE] 	= "WARNING_ACTIVE",
	[GATES_LOWERING] 	= "GATES_LOWERING",
	[GATES_DOWN] 		= "GATES_DOWN",
	[TRAIN_CROSSING] 	= "TRAIN_CROSSING",
	[TRAIN_CLEAR_WAIT] 	= "TRAIN_CLEAR_WAIT",
	[GATES_RAISING] 	= "GATES_RAISING",
	[X1_CLEAR] 			= "X1_CLEAR",
	[X1_FAULT] 			= "X1_FAULT"
};

state_t cur_state 	= IDLE;
state_t next_state 	= IDLE;
// --------------------------- END Crossing States Definition ------------------------------- //

/*
	If cur_state and next_state are different, then it generates a message
	to be stored in the log_buffer that is sent to the central messenger
	returns EXIT_SUCCESS when states were different and message was successful
	returns EXIT_FAILURE when states were the same.
*/
int state_transition_message(void)
{
	// should i be worried about mutexes? what else accesses this?
	if (cur_state != next_state)
	{
		printf("[State] (%d) %s -> (%d) %s\n", cur_state, state_to_string[cur_state], next_state, state_to_string[next_state]);
		int message = ((cur_state) + (next_state * 10));
		add_to_log_buffer(&pulses_to_central, (uint32_t)message);
		return EXIT_SUCCESS;
	}
	else
	{
		return EXIT_FAILURE;
	}
	
}

// Crossing state machine thread. arg is the shared events_t (&ev)
// Pulses the crossing server (self_con_details) every loop so it reruns its req processing
void *state_transitioner(void *arg)
{
	events_t *ev = (events_t *)arg;

	while(1)
	{
		// need to set the event code appropriately. Should I deal with errors for send_update_pulses?
		send_update_pulses(&self_con_details, SELF_WAKE_PULSE);
		switch (cur_state)
		{
			case IDLE:
			{
				// Wait until train_approaching
				if (pthread_mutex_lock(&ev->mutex) == EOK)
				{
					while (ev->events[EV_TRAIN_APPROACH] == 0 && ev->events[EV_X1_FAULT] == 0)
						pthread_cond_wait(&ev->cond, &ev->mutex);

					// set up the next state
					if (ev->events[EV_X1_FAULT] == EV_X1_FAULT_STATE_ON)
					{
						next_state = X1_FAULT;
					}
					else
					{
						next_state = TRAIN_APPROACHING;
						// Reset the event indicator? now or later?
						ev->events[EV_TRAIN_APPROACH] = 0;
					}
					pthread_mutex_unlock(&ev->mutex);
				}
				break;
			}
			case TRAIN_APPROACHING:
			{
				// Until warnings are active, wait on this state. Message controller will set 
				// EV_WARNINGS_ACTIVE when intersections inform they are done
				// If during this, the train reaches some error it will post the EV_X1_FAULT event
				// Then you active your warnings as well (flashers)
				if (pthread_mutex_lock(&ev->mutex) == EOK)
				{
					while (ev->events[EV_WARNINGS_ACTIVE] == 0 && ev->events[EV_X1_FAULT] == 0)
						pthread_cond_wait(&ev->cond, &ev->mutex);
					if (ev->events[EV_X1_FAULT] == EV_X1_FAULT_STATE_ON)
					{
						next_state = X1_FAULT;
					}
					else
					{
						next_state = WARNING_ACTIVE;
						ev->events[EV_WARNINGS_ACTIVE] = 0;
					}
					pthread_mutex_unlock(&ev->mutex);
				}
				
				// If while activating flashers a critical failure happened set the EV_X1_FAULT event and go to X1_FAULT state
				// setting the fault state will inform the train that a fault has occured.
				// **Yes both use the same fault event**
				if (activate_flashers() != EXIT_SUCCESS)
				{
					if (pthread_mutex_lock(&ev->mutex) == EOK)
					{
						ev->events[EV_X1_FAULT] = EV_X1_FAULT_STATE_ON;
						pthread_cond_broadcast(&ev->cond);
						pthread_mutex_unlock(&ev->mutex);
					}
					printf("[State: Error] Fatal fault encountered when activating flashers. Entering X1_FAULT.\n");
					next_state = X1_FAULT;
					// Need to deal with situation i fail to get the mutex lock?
					// Should i cancel the state_transitioner? with exit_failure???
				}
				break;
			}
			case WARNING_ACTIVE:
			{
				// ??? delay???
				next_state = GATES_LOWERING;
				if (gates_down() != EXIT_SUCCESS)
				{
					if (pthread_mutex_lock(&ev->mutex) == EOK)
					{
						ev->events[EV_X1_FAULT] = EV_X1_FAULT_STATE_ON;
						pthread_mutex_unlock(&ev->mutex);
						pthread_cond_broadcast(&ev->cond);
					}
					printf("[State: Error] Fatal fault encountered when closing gates. Entering X1_FAULT.\n");
					next_state = X1_FAULT;
				}
				break;
			}
			case GATES_LOWERING:
			{
				// Tells the server's APPROACH_NOTIFY req (RUNNING_S2) that it is safe to reply to the train
				if (pthread_mutex_lock(&ev->mutex) == EOK)
				{
					ev->events[EV_GATE_DOWN] = EV_GATE_DOWN_STATE_DOWN;
					pthread_mutex_unlock(&ev->mutex);
				}
				next_state = GATES_DOWN;
				break;
			}
			case GATES_DOWN:
			{
				if (pthread_mutex_lock(&ev->mutex) == EOK)
				{
					while (ev->events[EV_TRAIN_CROSSING] == 0 && ev->events[EV_X1_FAULT] == 0)
						pthread_cond_wait(&ev->cond, &ev->mutex);
					if (ev->events[EV_X1_FAULT])
					{
						next_state = X1_FAULT;
					}
					else
					{
						next_state = TRAIN_CROSSING;
						ev->events[EV_TRAIN_CROSSING] = 0;
					}
					pthread_mutex_unlock(&ev->mutex);
				}
				// Some message
				break;
			}
			case TRAIN_CROSSING:
			{
				if (pthread_mutex_lock(&ev->mutex) == EOK)
				{
					while (ev->events[EV_TRAIN_EXIT] == 0 && ev->events[EV_X1_FAULT] == 0)
						pthread_cond_wait(&ev->cond, &ev->mutex);
					if (ev->events[EV_X1_FAULT])
					{
						next_state = X1_FAULT;
					}
					else
					{
						next_state = TRAIN_CLEAR_WAIT;
						ev->events[EV_TRAIN_EXIT] = 0;
					}
					pthread_mutex_unlock(&ev->mutex);
				}

				break;
			}
			case TRAIN_CLEAR_WAIT:
				// sleep() for some time?... something that can be woken tho... cond_timed_wait()?
				next_state = GATES_RAISING;
				break;
			case GATES_RAISING:
				if (pthread_mutex_lock(&ev->mutex) == EOK)
				{
					if (ev->events[EV_X1_FAULT] == EV_X1_FAULT_STATE_ON)
					{
						next_state = X1_FAULT;
					}
					else
					{
						next_state = X1_CLEAR;
					}
					pthread_mutex_unlock(&ev->mutex);
				}
				if (next_state == X1_CLEAR)
					gates_up();
				break;
			case X1_CLEAR:
				// some message
				next_state = IDLE;
				break;
			case X1_FAULT:
			{
				// ???? is this ok. idk. well see
				activate_flashers();
				gates_down();
				return (void *)EXIT_FAILURE;
				break;
			}
			default:
			{
				break;
			}
		}

		// send message to the central controller
		state_transition_message();
		cur_state = next_state;
	}

	return NULL;
}

int activate_flashers(void)
{
	printf("[HW] Flashers activated.\n");
	return EXIT_SUCCESS;
}

int gates_down(void)
{
	printf("[HW] Gates down.\n");
	return EXIT_SUCCESS;
}
void gates_up(void)
{
	printf("[HW] Gates up.\n");
}
