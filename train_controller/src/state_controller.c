#include <stdio.h>
#include <stdint.h>

#include "state_controller.h"


// --------------------------- START Train States Definition ------------------------------- //
const char *state_to_string[] =
{
	[NRML] 			= "NORMAL",
	[SYS_FAIL] 		= "SYSTEM_FAILURE",
	[APRCH] 		= "APPROACHING_CROSSING",
	[EM_B_CROSS] 	= "EMERGENCY_BEFORE_CROSSING",
	[CROSS] 		= "CROSSING",
	[EM_A_CROSS] 	= "EMERGENCY_AT_CROSSING"
};

state_t cur_state 	= NRML;
state_t next_state 	= NRML;
// --------------------------- END Train States Definition ------------------------------- //


// , APRCHNG, EMERG, CROSSNG, PAST
// NRML = 0, EM_NRML, APRCH, EM_B_CROSS, CROSS, EM_A_CROSS, SUCC_CROSS

// train op normal ->
int state_controller_events(events_t *ev)
{
	uint8_t change_bool = 1;
	switch(*ev)
	{
		//------------------------------------------------------------------------//
		case DEFAULT:
			change_bool = 0;
			printf("[System] Received DEFAULT event. Maintaining current state (%d) %s\n", cur_state, state_to_string[cur_state]);
			break;
		//------------------------------------------------------------------------//
		case APRCHNG:
			if (cur_state == NRML)
				next_state = APRCH;
			else
				change_bool = 0;

			if (change_bool == 0)
			{
				printf("[System] Ignoring APRCH event. Only applicable during normal state\n");
			}
			else
			{
				printf("[System] Received APRCH event. Changing state from (%d) %s to (%d) %s\n", cur_state,
						state_to_string[cur_state], next_state, state_to_string[next_state]);
			}
			break;
		//------------------------------------------------------------------------//
		case EMERG:
			if (cur_state == APRCH)
				next_state = EM_B_CROSS;
			else if (cur_state == CROSS)
				next_state = EM_A_CROSS;
			else
				change_bool = 0;


			if (change_bool == 0)
			{
				printf("[System] Ignoring EMERG event. Only applicable at (or after) approaching and before successful crossing\n");
			}
			else
			{
				printf("[System] Received EMERG event. Changing state from (%d) %s to (%d) %s\n", cur_state,
						state_to_string[cur_state], next_state, state_to_string[next_state]);
			}

			// MsgReply to initiate subserver_central_messager. Might need a queue
			break;
		//------------------------------------------------------------------------//
		case CROSSNG:
			if (cur_state == APRCH)
				next_state = CROSS;
			else
				change_bool = 0;


			if (change_bool == 0)
			{
				printf("[System] Ignoring CROSSING event. Only applicable at approaching\n");
			}
			else
			{
				printf("[System] Received CROSSING event. Changing state from (%d) %s to (%d) %s\n", cur_state,
						state_to_string[cur_state], next_state, state_to_string[next_state]);
			}

			// MsgReply to initiate subserver_central_messager. Might need a queue
			break;
		//------------------------------------------------------------------------//
		case PAST:
			if (cur_state == CROSS)
				next_state = NRML;
			else
				change_bool = 0;


			if (change_bool == 0)
			{
				printf("[System] Ignoring PAST event. Only applicable at CROSSING\n");
			}
			else
			{
				printf("[System] Received PAST event. Changing state from (%d) %s to (%d) %s\n", cur_state,
						state_to_string[cur_state], next_state, state_to_string[next_state]);
			}
			// MsgReply to initiate subserver_central_messager. Might need a queue
			break;
		//------------------------------------------------------------------------//
		case FIXED:
			if (cur_state == EM_B_CROSS)
				next_state = APRCH;
			else if (cur_state == EM_A_CROSS)
				next_state = CROSS;
			else
				change_bool = 0;

			if (change_bool == 0)
			{
				printf("[System] Ignoring FIXED event. Only applicable at EM_B_CROSS and EM_A_Crossing\n");
			}
			else
			{
				printf("[System] Received FIXED event. Changing state from (%d) %s to (%d) %s\n", cur_state,
						state_to_string[cur_state], next_state, state_to_string[next_state]);
			}
			// Prepare message for pulse to central controller

			break;
		case CRITICAL_FAILURE:
			next_state = SYS_FAIL;

			printf("[System] Received CRITICAL_FAILURE event. Changing state from (%d) %s to (%d) %s\n", cur_state,
						state_to_string[cur_state], next_state, state_to_string[next_state]);
			break;
		//------------------------------------------------------------------------//
		default:
			printf("[Error] Invalid events_t received: %d\n", *ev);
			change_bool = 0;
			break;
	}


	if (change_bool == 1)
	{
		// Prepare message for central controller
		return ((cur_state) + (next_state * 10));
	}
	else
	{
		return -1;
	}


}
