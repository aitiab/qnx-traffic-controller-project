#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>

#include "message_handling.h"
#include "state_controller.h"
#include "input.h"
#include "message_controller.h"


// cancellation only allowed while blocked on wait for terminal signal. main should never cancel a transition halfway
// should i worry about setcancelstate returning an error?
static void wait_for_event(events_t *ev)
{
	// since readInput may get straved at the condwait due to terminal failure
	// allow the cancellation at the wait,  
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	readInput(ev);
	// past the stravation point
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
}

// Child state machine. Runs until it reaches SYS_FAIL or is cancelled by main.
void *child_sm_controller(void *arg)
{
	// start on cancel disabled to keep state machine stable.
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	events_t ev = DEFAULT;
	int message = -1; // the pulse value to be send to central controller
	while (cur_state != SYS_FAIL)
	{
		switch (cur_state){
		case NRML:
			wait_for_event(&ev);
			message = state_transitioner(&ev);
			if (message != -1)
				add_to_log_buffer(&pulses_to_central, (uint32_t)message);
			break;
		case APRCH:
			wait_for_event(&ev);
			message = state_transitioner(&ev);
			if (message != -1)
				add_to_log_buffer(&pulses_to_central, (uint32_t)message);
			break;
		case EM_B_CROSS:
			wait_for_event(&ev);
			message = state_transitioner(&ev);
			if (message != -1)
				add_to_log_buffer(&pulses_to_central, (uint32_t)message);
			break;
		case CROSS:
			wait_for_event(&ev);
			message = state_transitioner(&ev);
			if (message != -1)
				add_to_log_buffer(&pulses_to_central, (uint32_t)message);
			break;
		case EM_A_CROSS:
			wait_for_event(&ev);
			message = state_transitioner(&ev);
			if (message != -1)
				add_to_log_buffer(&pulses_to_central, (uint32_t)message);
			break;
		default:
			printf("[Warning] Train controller is on an undefined state %d. Changing state back to NRML.\n", cur_state);
			next_state = NRML;
		}

		cur_state = next_state;


	}

	// return nuLL or something else?
	return (cur_state == SYS_FAIL) ? (void *)EXIT_FAILURE : (void *)EXIT_SUCCESS;
}


int main(void) {
	errno = EOK;

	pthread_t train_sense_system_tid, central_massenger_tid, child_sm_controller_tid;
	int rc;

	// Maybe set to lowest priority.
	rc = pthread_create(&central_massenger_tid, NULL, subserver_central_messenger, NULL);
	if (rc != EOK)
	{
		printf("[Error] Failed to create thread for subserver_central_messenger: %s\n", strerror(rc));
		// Fail silently
	}

	// should be higher priority than central messenger given it maintains the state.
	// if "child" state machine failed to be created, then place the train in SYS_FAIL state.
	void *child_sm_controller_status = (void *)NULL;
	rc = pthread_create(&child_sm_controller_tid, NULL, child_sm_controller, NULL);
	if (rc != EOK)
	{
		printf("[Error] Failed to create thread for state machine: %s\nExiting with FAIL\n", strerror(rc));
		cur_state = SYS_FAIL;
		child_sm_controller_status = (void *)EXIT_FAILURE;


		// Should the main thread exit_failure? or should it go into some infinite loop? 
		return EXIT_FAILURE;
	}

	// higher priority than usual needed??
	void *train_sense_status = (void *)NULL;
	rc = pthread_create(&train_sense_system_tid, NULL, train_sense_system, NULL);
	if (rc != EOK)
	{
		printf("[Error] Failed to create thread for terminal input reader: %s\n", strerror(rc));
		train_sense_status = (void *)EXIT_FAILURE; // redundant but for clarity
	}

	// If train_sense_system failed, then thread_exited = 1 setups up the next steps to cancel state machine etc.
	uint8_t thread_exited = train_sense_status == NULL ? 0 : 1;

	// A very trashy way to wait for either thread to exit and deal with it correspondingly.
	struct timespec deadline;
	while(thread_exited == 0)
	{
		// is 5 seconds a good timeout? train losing info around it for 5 seconds?
		clock_gettime(CLOCK_REALTIME, &deadline);
		deadline.tv_sec += 5; // 5 seconds timeout for join
		int rc = pthread_timedjoin(train_sense_system_tid, &train_sense_status, &deadline);
		
		// If thread successfully joined, or join failed due to an error other than timeout and detached thread,
		// then stop the state machine, set state to SYS_FAIL and exit loop.
		if (rc == EOK || (rc != ETIMEDOUT && rc != EINVAL))
		{
			printf("[System] Pthread_timedjoin on train_sense_system returned %d.\nWill terminate state machine thread.\n", rc);
			thread_exited = 1; // indicate train_sense_system exited and before state machine
		}
		else 
		{
			clock_gettime(CLOCK_REALTIME, &deadline);
			deadline.tv_sec += 5; // 5 seconds timeout for join
			rc = pthread_timedjoin(child_sm_controller_tid, &child_sm_controller_status, &deadline);
			if (rc == EOK || (rc != ETIMEDOUT && rc != EINVAL))
			{	
				printf("[System] Pthread_timedjoin on state machine returned %d.\nWill terminate train_sense_system thread.\n", rc);
				thread_exited = 2; // indicate state machine exited and before train_sense_system
			}	 
		}
	}

	
	if (thread_exited == 1)
	{
		// the input is the trains sensors and things. so without it, train should not be running
		// If the thread fails to create, or joins (exists) then should safely cancel the state machine
		// safely here means transition related ops are completed (cur_state/next_state are stable)
		// train_sense_system thread fails if fgets fails, which should broadcast to get state machine out of cond_wait
		// If the state machine already reached SYS_FAIL on its own, it has exited and this just reaps it.
		pthread_cancel(child_sm_controller_tid);
		// sets child_sm_controller_status to PTHREAD_CANCELED if it was cancelled (reached a cancellation point)
		// or EXIT_FAILURE if it exited on its own (reached SYS_FAIL)
		pthread_join(child_sm_controller_tid, &child_sm_controller_status); 

		// if train_sense_system failed then closed (status = EXIT_FAILURE), or thread failed to join (status = NULL)
		// then place train in SYS_FAIL state.
		if (((intptr_t)train_sense_status == EXIT_FAILURE || train_sense_status == NULL) && cur_state != SYS_FAIL)
		{
			events_t ev = CRITICAL_FAILURE;
			int message = state_transitioner(&ev);
			cur_state = next_state;
			if (message != -1)
				add_to_log_buffer(&pulses_to_central, (uint32_t)message);
		}
	}
	else if (thread_exited == 2)
	{
		// state machine exited on its own. so just cancel the train_sense_system thread and exit
		pthread_cancel(train_sense_system_tid);
		pthread_join(train_sense_system_tid, &train_sense_status);
	}

	// Need to worry about letting central messenger finish
	pthread_join(central_massenger_tid, NULL);

	return (cur_state == SYS_FAIL) ? EXIT_FAILURE : EXIT_SUCCESS;
}


// Message to the train crossing


