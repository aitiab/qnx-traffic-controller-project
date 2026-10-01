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
	.count 		= 0,
	.events 	= {0},
	.nextRead 	= 0,
	.nextWrite	= 0,
	.status 	= STATUS_RUNNING
};


void readInput(events_t *ev)
{
	if (pthread_mutex_lock(&(input_obj.mutex)) == EOK)
	{
		while (input_obj.status == STATUS_RUNNING && input_obj.count == 0)
			pthread_cond_wait(&input_obj.cond, &input_obj.mutex);

		if (input_obj.status == STATUS_RUNNING)
		{
			*ev = input_obj.events[input_obj.nextRead];
			input_obj.nextRead = (input_obj.nextRead + 1) % EVENT_BUFF_SIZE;
			input_obj.count--;
		}
		else
		{
			*ev = CRITICAL_FAILURE;
		}

		pthread_mutex_unlock(&(input_obj.mutex));
	}
	else
	{
		printf("[Warning] Failed to get mutex for readInput. Returning DEFAULT event\n");
		*ev = CRITICAL_FAILURE; // or default?
	}
}

// Input system
void *terminal_in(void *arg)
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
			else if (strcasecmp(line, "") == 0)
			{
				_event = DEFAULT;
			}
			else
			{
				printf("[Warning] Unknown command: %s\n", line);
				continue;
			}

			if (pthread_mutex_lock(&(input_obj.mutex)) == EOK)
			{
				if (input_obj.count < EVENT_BUFF_SIZE)
				{
					input_obj.events[input_obj.nextWrite] = _event;
					input_obj.nextWrite = (input_obj.nextWrite + 1) % EVENT_BUFF_SIZE;
					input_obj.count++;

					success_flag = 1;

					pthread_cond_signal(&input_obj.cond);
				}
				else
				{
					success_flag = -1;
				}
				pthread_mutex_unlock(&(input_obj.mutex));
			}
			else
			{
				printf("[Warning] Failed to get mutex for adding event to input_obj. The event is dropped\n");
			}

			if (success_flag == -1)
			{
				printf("[Warning] input_obj buffer is full. The new event is dropped\n");
			}

		}
		else
		{

			if (ferror(stdin) && errno == EINTR)
			{
				clearerr(stdin);
				continue;
			}

			// Get readInput out of the cond_wait
			// stop readInput checking the status before its updated
			pthread_mutex_lock(&input_obj.mutex);
			input_obj.status = STATUS_FAILED;
			pthread_cond_broadcast(&input_obj.cond); // send to all waiting on the cond
			pthread_mutex_unlock(&input_obj.mutex);

			printf("[Error]: fgets() returned NULL. Error is: %s\n", strerror(errno));
			printf("Fatal error stopping reading stdin in terminal_in.\n");
			return (void *)EXIT_FAILURE;
		}
	}

	return NULL;
}
