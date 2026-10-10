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
#include "hardware.h"


events_t ev = {.events = {0}, .mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER};
crossing_gates_t crossing_gates = {.cur_state = GATES_RAISE, .next_state = GATES_NOT_SET, .mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER};

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
	gates_req(&crossing_gates, GATES_LOWER);

	// pretty bad...
	// maybe should periodically sent updates to central controller?
	while (1)
		sleep(5);
}
int main(void) {

	// ---------------------------------------- //
	// Configure important global things for the controller 
	errno = EOK; // do i need this?

	pthread_condattr_t _cond_attr;
	pthread_condattr_init(&_cond_attr);
	pthread_condattr_setclock(&_cond_attr, CLOCK_MONOTONIC);
	if (pthread_cond_init(&ev.cond, &_cond_attr) != EOK)
	{
		printf("[Main: Error] Failed to init cond for events_t: %s\nMoving to X1_FAULT.\n", strerror(errno));
		master_move_to_X1_FAULT();
		return EXIT_FAILURE;
	}
	// ---------------------------------------- //


	sem_t server_established_indicator;
	sem_init(&server_established_indicator, 0, 0);

	pthread_t crossing_server_tid, state_transitioner_tid, central_messenger_tid, crossing_gates_tid;
	int rc;

	rc = pthread_create(&central_messenger_tid, NULL, subserver_central_messenger, NULL);
	if (rc != EOK)
	{
		printf("[Main: Error] Failed to create thread for subserver_central_messenger: %s\n", strerror(rc));
		// Fail silently
	}

	// Crossing gates thread.
	crossing_gates_controller_data_t gates_d = {.gates = &crossing_gates, .ev = &ev};
	rc = pthread_create(&crossing_gates_tid, NULL, crossing_gates_controller, (void *)(&gates_d));
	if (rc != EOK)
	{
		printf("[Main: Error] Failed to create thread for crossing_gates_controller: %s\nMoving to X1_FAULT.\n", strerror(rc));
		master_move_to_X1_FAULT();
		return EXIT_FAILURE;
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
	// since sem_wait waits for the message_controller to update the status
	// Checking the status after is safe
	if (crossing_server_details.status == STATUS_FAILED)
	{
		printf("[Main: Error] Crossing Server failed to establish, moving state to X1_FAULT\n");
		// if status is FAILED, then we dont need to cancel the thread
		// it should returned with EXIT_FAILURE 
		pthread_join(crossing_server_tid, &crossing_server_status);
		master_move_to_X1_FAULT();
		return EXIT_FAILURE; // guess this is useless
	}
	else
	{
		// Self connection, used by the state machine to pulse the server so it re-runs its req processing
		// the func is synchronous. so wait for the return.
		// shoould there be considerations about timing/how long it takes for name_open to return 
		rc = establish_connection(&self_con_details);
		if (rc != EXIT_SUCCESS)
		{
			// Without self wake pulses, APPROACH_NOTIFY reqs cant progress past RUNNING_S2, so treat as fatal
			printf("[Main: Error] Failed to connect to own crossing server: %s\nMoving to X1_FAULT.\n", strerror(rc));
			master_move_to_X1_FAULT();
			return EXIT_FAILURE;
		}
		// should i do admittance?
		// probs not unless im sending messages and elaborate processing of my requests.
	}
	

	// Doesnt make sense to remove or cancel crossing or central if state transitioner crashes or fails to open.
	// since talking to the train and central is still important
	state_transitioner_data_t st_d = {.ev = &ev, .gates = &crossing_gates};
	void *state_transitioner_status = (void *)NULL;
	rc = pthread_create(&state_transitioner_tid, NULL, state_transitioner, (void *)(&st_d));
	if (rc != EOK)
	{
		printf("[Main: Error] Failed to create thread for state_transitioner: %s\nExiting with FAIL\n", strerror(rc));
		//pthread_join()
		master_move_to_X1_FAULT();
		return EXIT_FAILURE;
	}

	/*
	In the event that the crossing server does crash...
	The state_transitioner should be cancelled
	Since cancellation only occurs in cancellation points (i.e. the cond_wait)
	Only need to worry about turning off cancel when doing updates to the central
	Also must ensure it doesn't die whilst holding a mutex (the crossing server)
	
	Yet another issue... central crashing... holding the mutex, and blocking the state_transitioner.
	but central gets messages from log as sync... lock and unlock?... central only?? crashes on send_pulses
	therefore by then the mutex is unlocked.
	so should be fine...

	just remember to cancel the cancellation around that event.
	add the cancellation handlders to the state_tranisitioner switch case...

	always safer to
	ensure (assert) the system is in the X1_FAULT state and safe
	*/
	rc = pthread_join(crossing_server_tid, &crossing_server_status);
	
	// If the pthread joined successfully or some error other than those indicating
	// the thread is detached (EINVAL) or pthread_join timed out (ETIMEDOUT)
	// then cancel 
	if (rc == EOK || (rc != ETIMEDOUT && rc != EINVAL))
	{
		printf("[Main: CRITICAL] Crossing server closed. Now closing state_transitioner.\n");
		pthread_cancel(state_transitioner_tid);
		pthread_join(state_transitioner_tid, &state_transitioner_status);
		printf("[Main: CRITICAL] Crossing server and state transitioner closed.\n");
		master_move_to_X1_FAULT();
		return EXIT_FAILURE; //to who? idk?
	}
	
	// Maybe one should deal with the situation where state_transitioner crashes first
	// of course in such an event master_move_to_x1_fault should occur after...
	// could do the same timedjoin... but thats iffy... idk
	// atm except things really sus happens, it is unlikely to crashy??
	pthread_join(state_transitioner_tid, &state_transitioner_status);

	//idk.
	return EXIT_SUCCESS;
}
