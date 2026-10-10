#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <stdint.h>

#include "message_handling.h"
#include "input.h"


// Initalise the input/sensor shared object
input_t input_obj = {
	.mutex 		= PTHREAD_MUTEX_INITIALIZER,
	.cond		= PTHREAD_COND_INITIALIZER,
	.ready 		= 0,
	.event 	= DEFAULT,
	.nextRead 	= 0,
	.nextWrite	= 0,
	.status 	= STATUS_RUNNING
};


// Pushed inside the thread cleanup handler
// so when caleld it releases the mutex if the thread
static void unlock_input_mutex(void *arg)
{
	pthread_mutex_unlock(&(input_obj.mutex));
}

void readInput(events_t *ev)
{
	if (pthread_mutex_lock(&(input_obj.mutex)) == EOK)
	{
		// in the case that input thread has failed and exited, there wont be a signal to the cond_wait below
		// when the input thread fails, main sets state machine to die on next cancellation point
		// which is the cond_wait below.
		// before it does die, it would call unlock_input_mutex to unlock the mutex
		pthread_cleanup_push(unlock_input_mutex, NULL);

		while (input_obj.status == STATUS_RUNNING && input_obj.ready == 0)
			pthread_cond_wait(&input_obj.cond, &input_obj.mutex);

		// If input system status is running, then continue as normal
		if (input_obj.status == STATUS_RUNNING)
		{
			// *ev = input_obj.events[input_obj.nextRead];
			// input_obj.nextRead = (input_obj.nextRead + 1) % EVENT_BUFF_SIZE;
			// input_obj.count--;
			*ev = input_obj.event;
			input_obj.ready = 0;
		}
		else // If input system status is not running, return a critical failure event to state machine
		{
			*ev = CRITICAL_FAILURE;
		}
		
		/* past the stravation point. if it has reached this point, then train_sense_system thread has not failed
			and exited, so it is safe to pop the cleanup handler
			popping the cleanup with a (1) will call the cleanup handler which will unlock the mutex
			just mimicing the usual pthread_mutex_unlock() call.
		*/
		pthread_cleanup_pop(1); // unlocks the mutex
	}
	else
	{
		printf("[IO: Error] Failed to get mutex for readInput. Returning CRITICAL_FAILURE event\n");
		/* CRITICAL_FAILURE or default? CRITICAL_FAILURE is more safe since loss of input means train doesnt
		 know what to do.
		 
		 Maybe need to check type of error. some might be recoverable. although the input would be lost, which 
		 is problem since it was critical sense data. so maybe CRITICAL_FAILURE is better.
		*/
		*ev = CRITICAL_FAILURE;  
	}
}

// Input system
void *train_sense_system(void *arg)
{
	int success_flag = 0;

	while(1)
	{
		success_flag = 0;

		char line[50];
		events_t _event = DEFAULT;

		printf("[IO] Provide input:\n");
		fflush(stdout);

		if (fgets(line, sizeof(line), stdin) != NULL)
		{
			// replace trailing newline with terminating
			line[strcspn(line, "\n")] = '\0';

			if (strcasecmp(line, "approaching") == 0) // need to make it trim() and lowercase
			{
				_event = APRCHNG;
			}
			else if (strcasecmp(line, "emergency") == 0)
			{
				_event = EMERG;
			}
			else if (strcasecmp(line, "crossing") == 0)
			{
				_event = CROSSNG;
			}
			else if (strcasecmp(line, "past") == 0)
			{
				_event = PAST;
			}
			else if (strcasecmp(line, "fixed") == 0)
			{
				_event = FIXED;
			}
			// else if (strcasecmp(line, "") == 0)
			// {
			// 	_event = DEFAULT;
			// }
			else
			{
				printf("[IO: Warning] Unknown command: %s\n", line);
				continue;
			}

			if (pthread_mutex_lock(&(input_obj.mutex)) == EOK)
			{
				// if old event is not yet read, and the old event was EMERG, then dont replace it.
				if (input_obj.ready == 1 && input_obj.event == EMERG)
				{
					printf("[IO: Warning] Ignoring event %d. Train is already in EMERG state.\n", _event);
					pthread_mutex_unlock(&(input_obj.mutex));
					continue;
				}
				input_obj.event = _event;
				input_obj.ready = 1;
				pthread_cond_signal(&input_obj.cond);
				pthread_mutex_unlock(&(input_obj.mutex));
			}
			else
			{
				printf("[IO: Warning] Failed to get mutex for adding event to input_obj. The event is dropped\n");
				if (pthread_mutex_lock(&input_obj.mutex) == EOK)
				{
					input_obj.status = STATUS_FAILED;
					pthread_cond_broadcast(&input_obj.cond); // send to all waiting on the cond
					pthread_mutex_unlock(&input_obj.mutex);
				}
				printf("[IO: Error] Fatal error, event was dropped, stopping train_sense_system.\n");
				return (void *)EXIT_FAILURE;
			}
		}
		else
		{

			if (ferror(stdin) && errno == EINTR)
			{
				clearerr(stdin);
				continue;
			}

			/* Get readInput out of the cond_wait
			 why mutexes around... not sure...
			
			 anyhow if the mutex lock fails, the thread exists anyhow
			 which then lets the main thread handling closing the state machine
			 and putting the train in SYS_FAIL state.

			 if mutex lock succeeds, the input system status is set to failed and cond unlocks the 
			 readInput func (state machine thread) and sends CRITICAL_FAILURE event to state machine
			 which also transitions to SYS_FAIL state.

			 so they are redundant but safe.
			 */
			if (pthread_mutex_lock(&input_obj.mutex) == EOK)
			{
				input_obj.status = STATUS_FAILED;
				pthread_cond_broadcast(&input_obj.cond); // send to all waiting on the cond
				pthread_mutex_unlock(&input_obj.mutex);
			}

			printf("[IO: Error] fgets() returned NULL. Error is: %s\n", strerror(errno));
			printf("[IO: Error] Fatal error, stopped reading stdin in train_sense_system.\n");
			return (void *)EXIT_FAILURE;
		}
	}

	// should it relly return NULL? the while loop is infinite so it reacches here we have big problems
	return NULL;
}
