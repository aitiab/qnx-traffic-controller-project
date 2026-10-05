#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "state_controller.h"
#include "message_controller.h"


// --------------------------- START Train States Definition ------------------------------- //
const char *state_to_string[] =
{
	[NRML] 			= "NORMAL",
	[SYS_FAIL] 		= "SYSTEM_FAILURE",
	[APRCH] 		= "APPROACHING_CROSSING",
	[CROSS] 		= "CROSSING"
};

state_t cur_state 	= NRML;
state_t next_state 	= NRML;
// --------------------------- END Train States Definition ------------------------------- //


/*
	Notifies the crossing controller that a fault has occured to the train
	Returns nothing. You should manage train transitions to safe fault states yourself 
*/
static void _notify_fault_to_crossing(server_con_details_t *crossing_details)
{
	mh_msg_t fault_notify_msg = {.client_identifier = crossing_details->client_identifier, .type = FAULT_NOTIFY, .subtype = 0, .data = 0};
	reply_t fault_reply = {0};
	int rc = send_message_timed(crossing_details, &fault_notify_msg, &fault_reply, MS_NOTIFY_FAULT);
	if (rc != EXIT_SUCCESS)
	{
		// If fault notification fails,
		printf("[Notify: Error] Failed to notify crossing of FAULT.\n");
	}
	else
	{
		// if the reply from crossing is EOK (NOTIFIED) or EAGAIN (REQ_BUFF_FULL) then put train in SYS_FAIL
		if (fault_reply.status == EOK)
		{
			printf("[Notify: Info] Crossing acknowledged fault notification.\n");
		}
		else
		{
			printf("[Notify: Error] Crossing failed to acknowledge fault notification.\n");
		}
	}
}

/*
	Notify the crossing of some event (as provided in notify_event).
	Returns EXIT_FAILURE if send fails (due to errors provided by send_message_timed)
	or returns the reply.status when send_message_timed was successful
*/
static int _notify_crossing(server_con_details_t *crossing_details, int notify_event, int MS_SEND_MESSAGE_TIMEOUT)
{
	mh_msg_t notify_msg = {.client_identifier = crossing_details->client_identifier, .type = notify_event, .subtype = 0, .data = 0};
	reply_t notify_reply = {0};
	int rc = send_message_timed(crossing_details, &notify_msg, &notify_reply, MS_SEND_MESSAGE_TIMEOUT);
	if (rc != EXIT_SUCCESS)
	{
		// If approach notification fails, put train in SYS_FAIL state next
		printf("[Notify: Warning] Failed to notify crossing server of event %d.\n", notify_event);
		return EXIT_FAILURE;
	}
	else
	{
		// if the reply from crossing is EFAULT (STOP_TRAIN) or EAGAIN (REQ_BUFF_FULL) then put train in SYS_FAIL
		if (notify_reply.status == EFAULT)
		{
			printf("[Notify: Warning] Crossing server notified us to stop the train.\n");
		}
		else if(notify_reply.status == EOK)
		{
			printf("[Notify: Info] Successfully notified crossing server of event %d.\n", notify_event);
		}

		return notify_reply.status;
	}
}

// , APRCHNG, EMERG, CROSSNG, PAST
// NRML = 0, EM_NRML, APRCH, EM_B_CROSS, CROSS, EM_A_CROSS, SUCC_CROSS

// train op normal ->
int state_transitioner(events_t *ev, server_con_details_t *crossing_details)
{
	uint8_t change_bool = 1;
	switch(*ev)
	{
		//------------------------------------------------------------------------//
		case DEFAULT:
		{
			change_bool = 0;
			printf("[Event: Info] DEFAULT event. Staying in (%d) %s\n", cur_state, state_to_string[cur_state]);
			break;
		}
		//------------------------------------------------------------------------//
		case APRCHNG:
		{
			if (cur_state == NRML)
			{
				int rc = _notify_crossing(crossing_details, APPROACH_NOTIFY, MS_NOTIFY_APPROACH);
				if (rc != EOK)
				{
					// If approach notification fails, put train in SYS_FAIL state next
					printf("[Notify: Error] Failed to notify crossing of approaching.\n");
					next_state = SYS_FAIL;
				}
				else if (rc == EOK)
				{
					printf("[Notify: Info] Notified crossing of approach.\n");
					next_state = APRCH;
				}
			}
			else
				change_bool = 0;
				

			if (change_bool == 0)
			{
				printf("[Event: Warning] Ignoring APRCH event. Only applicable during normal state\n");
			}
			else
			{
				printf("[State] (%d) %s -> (%d) %s  (on APRCH event)\n", cur_state,
						state_to_string[cur_state], next_state, state_to_string[next_state]);
			}
			break;
		}
		//------------------------------------------------------------------------//
		case EMERG:
			// any emergency notifies the crossing and puts the train in SYS_FAIL
			// yes this is not ideal... maybe it should have a bit more difference to CRITICAL_FAILURE
			// (same as CRITICAL_FAILURE for now)
			_notify_fault_to_crossing(crossing_details);
			// sets next_state = SYS_FAIL
			next_state = SYS_FAIL;
			printf("[State] (%d) %s -> (%d) %s  (on EMERG event)\n", cur_state,
					state_to_string[cur_state], next_state, state_to_string[next_state]);
			break;
		//------------------------------------------------------------------------//
		case CROSSNG:
			if (cur_state == APRCH)
			{
				int rc = _notify_crossing(crossing_details, CROSSING_NOTIFY, MS_NOTIFY_CROSSING);
				if (rc != EOK)
				{
					// If approach notification fails, put train in SYS_FAIL state next
					printf("[Notify: Error] Failed to notify crossing server of train crossing.\n");
					next_state = SYS_FAIL;
				}
				else if (rc == EOK)
				{
					printf("[Notify: Info] Notified crossing server of train crossing.\n");
					next_state = CROSS;
				}
			}
			else
				change_bool = 0;


			if (change_bool == 0)
			{
				printf("[Event: Warning] Ignoring CROSSING event. Only applicable at approaching\n");
			}
			else
			{
				printf("[State] (%d) %s -> (%d) %s  (on CROSSING event)\n", cur_state,
						state_to_string[cur_state], next_state, state_to_string[next_state]);
			}

			// MsgReply to initiate subserver_central_messager. Might need a queue
			break;
		//------------------------------------------------------------------------//
		case PAST:
			if (cur_state == CROSS)
			{
				int rc = _notify_crossing(crossing_details, EXIT_NOTIFY, MS_NOTIFY_EXIT);
				if (rc != EOK)
				{
					// If approach notification fails, put train in SYS_FAIL state next
					printf("[Notify: Error] Failed to notify crossing server of exit.\n");
					next_state = SYS_FAIL;
				}
				else if (rc == EOK)
				{
					printf("[Notify: Info] Notified crossing server of exit.\n");
					next_state = NRML;
				}
			}
			else
				change_bool = 0;


			if (change_bool == 0)
			{
				printf("[Event: Warning] Ignoring PAST event. Only applicable at CROSSING\n");
			}
			else
			{
				printf("[State] (%d) %s -> (%d) %s  (on PAST event)\n", cur_state,
						state_to_string[cur_state], next_state, state_to_string[next_state]);
			}
			// MsgReply to initiate subserver_central_messager. Might need a queue
			break;
		//------------------------------------------------------------------------//
		case FIXED:
			// No emergency states to recover from (EMERG goes straight to SYS_FAIL), so FIXED does nothing for now
			change_bool = 0;
			printf("[Event: Info] Ignoring FIXED event. No emergency states to recover from\n");
			break;
		case CRITICAL_FAILURE:
			_notify_fault_to_crossing(crossing_details);
			// sets next_state = SYS_FAIL
			next_state = SYS_FAIL;

			printf("[State] (%d) %s -> (%d) %s  (on CRITICAL_FAILURE event)\n", cur_state,
						state_to_string[cur_state], next_state, state_to_string[next_state]);
			break;
		//------------------------------------------------------------------------//
		default:
			printf("[Event: Error] Invalid events_t received: %d\n", *ev);
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
