#include <stdio.h>
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



// Crossing state machine thread. arg is the shared events_t (&ev).
// Pulses the crossing server (self_con_details) every loop so it re-runs its req processing.
void *state_transitioner(void *arg)
{
	events_t *ev = (events_t *)arg;

	while(1)
	{
		// need to set the event code appropriately. Should I deal with errors for send_update_pulses?
		send_update_pulses(&self_con_details, 0);
		switch (cur_state)
		{
			case IDLE:
			{
				// Wait until train_approaching
				if (pthread_mutex_lock(&ev->mutex) == EOK)
				{
					while (ev->events[EV_TRAIN_APPROACH] == 0)
						pthread_cond_wait(&ev->cond, &ev->mutex);

					// set up the next state
					next_state = TRAIN_APPROACHING;
					// Reset the event indicator? now or later?
					ev->events[EV_TRAIN_APPROACH] = 0;
					pthread_mutex_unlock(&ev->mutex);
				}
				break;
			}
			case TRAIN_APPROACHING:
			{
				// when should flashers be activated?
				// Wait until warnings are active
				if (pthread_mutex_lock(&ev->mutex) == EOK)
				{
					while (ev->events[EV_WARNINGS_ACTIVE] == 0)
						pthread_cond_wait(&ev->cond, &ev->mutex);

					next_state = WARNING_ACTIVE;
					ev->events[EV_WARNINGS_ACTIVE] = 0;
					pthread_mutex_unlock(&ev->mutex);
				}
				activate_flashers();
				break;
			}
			case WARNING_ACTIVE:
			{
				// ??? delay???
				next_state = GATES_LOWERING;
				break;
			}
			case GATES_LOWERING:
			{
				gates_down();
				// Tells the server's APPROACH_NOTIFY req (RUNNING_S2) that it is safe to reply to the train
				if (pthread_mutex_lock(&ev->mutex) == EOK)
				{
					ev->events[EV_GATE_DOWN] = 1;
					pthread_mutex_unlock(&ev->mutex);
				}
				next_state = GATES_DOWN;
				break;
			}
			case GATES_DOWN:
			{
				if (pthread_mutex_lock(&ev->mutex) == EOK)
				{
					while (ev->events[EV_TRAIN_CROSSING] == 0)
						pthread_cond_wait(&ev->cond, &ev->mutex);

					next_state = TRAIN_CROSSING;
					ev->events[EV_TRAIN_CROSSING] = 0;
					pthread_mutex_unlock(&ev->mutex);
				}
				// Some message
				break;
			}
			case TRAIN_CROSSING:
			{
				if (pthread_mutex_lock(&ev->mutex) == EOK)
				{
					while (ev->events[EV_TRAIN_EXIT] == 0)
						pthread_cond_wait(&ev->cond, &ev->mutex);

					next_state = TRAIN_CLEAR_WAIT;
					ev->events[EV_TRAIN_EXIT] = 0;
					pthread_mutex_unlock(&ev->mutex);
				}

				break;
			}
			case TRAIN_CLEAR_WAIT:
				// sleep() for some time
				next_state = GATES_RAISING;
				break;
			case GATES_RAISING:
				gates_up();
				next_state = X1_CLEAR;
				break;
			case X1_CLEAR:
				// some message
				next_state = IDLE;
				break;
			default:
			{
				break;
			}
		}

		cur_state = next_state;
		// send message to the central controller
	}

	return NULL;
}

void activate_flashers(void)
{
	printf("[System] Flashers activated.\n");
}

void gates_down(void)
{
	printf("[System] Gates down.\n");
}
void gates_up(void)
{
	printf("[System] Gates up.\n");
}
