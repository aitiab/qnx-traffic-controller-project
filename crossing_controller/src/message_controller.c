#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>

#include "message_controller.h"


log_buffer_t pulses_to_central = {
	.mutex 	= PTHREAD_MUTEX_INITIALIZER,
	.cond 	= PTHREAD_COND_INITIALIZER
};

// Connection details to central controller
server_con_details_t central_con_details = {
	.established = 0,
	.coid = -1,
	.client_identifier = CROSSING_CONTROLLER_CLIENT_ID, // need to discuss with the central controller
	.sname = QNET_CENTRAL_CONTROLLER_ATTACH_POINT,
	.status = STATUS_RUNNING
};


server_create_details_t crossing_server_details = {
	.name = CROSSING_SERVER_ATTACH_POINT,
	.established = 0,
	.attach = NULL,
	.status = STATUS_RUNNING
};

server_con_details_t self_con_details = {
	.established = 0,
	.coid = -1,
	.client_identifier = CROSSING_CONTROLLER_CLIENT_ID, 
	.sname = CROSSING_SERVER_ATTACH_POINT, // What's the consequence of this?
	.status = STATUS_RUNNING // should it be running on start? 
};


static void _disconnect_handler(void *data);
static void _unblock_handler(void *data);
static void client_handler_initaliser(client_details_t *ct);
static int _my_message_handler(client_dict_t *client_dict, struct _msg_info *info, int rcvid, recv_t *recv);
static int _my_req_process(client_dict_t *client_dict, events_t *ev);
static int _reply_req_done(client_details_t *ct, int rcvid, int err_code);
static int _reply_req_fail(client_details_t *ct, int rcvid, int err_code);

/* 
* A bit sussy that except inital failure to establish I couldnt find any other fatal failure situations...
* except the mutex parts.
* currently failed msgReply are dealt with MsgError where the error status send back essentially carries the same message/indicate to the train
* i guess the real error (fatal) would be if the reply carried important data rather than simple indicators.
*/ 

void *server_crossing_controller(void *arg)
{
	server_crossing_controller_data *_d = (server_crossing_controller_data *)arg;
	events_t *ev = _d->ev;
	sem_t *sem = _d->sem;

	// Need to worry about if establish_server fails, it should send the train in X1_FAULT state.
	// establish_server returns EXIT_SUCCESS is succeed, otherwise passes down the errno
	int rc = establish_server(&crossing_server_details);
	if (rc != EXIT_SUCCESS)
	{
		crossing_server_details.status = STATUS_FAILED;
		// Also be used as a synchronisation primitive for the crossing_server_details.status.
		// The status will then be checked by crossing_controller.c
		sem_post(sem);
		printf("[CrossServer: Error] Failed to establish server for the crossing controller...\n\"server_crossing_controller\" thread is exiting.\n");
		return (void *)EXIT_FAILURE;
	}

	// Indicate the server has been established. (Could use a barrier?)
	sem_post(sem);

	// will this {0} set both count and the inner struct?
	client_dict_t client_dict = {0};

	recv_t recv;
	while (1)
	{
		struct _msg_info info;
		int rcvid = MsgReceive(crossing_server_details.attach->chid, &recv, sizeof(recv), &info);
		if (rcvid == -1)
		{
			printf("[CrossServer: Warning] Failed to receive message. Error is %s\n", strerror(errno));
			continue;
		}

		if (rcvid == 0)
		{
			// Pulse received
			printf("[CrossServer: Info] Pulse received (scoid: %d, code: %d, value: %d)\n", recv.pulse.scoid, recv.pulse.code, recv.pulse.value.sival_int);
			
			client_details_t *ct = client_dict_lookup_scoid(&client_dict, recv.pulse.scoid);
			
			// // is this valid? shouldnt it depend on the kernal limit? 
			// if (ct == NULL)
			// {
			// 	printf("[CrossServer: Error] Clients dict is full. Will drop this pulse. Might be fatal\n");
			// }
			
			client_handler_data_t ct_d = {.ct = ct, .data = ev};

			switch (recv.pulse.code)
			{
				// Find someway to connect clientID with scoid.
				// Use value to let client decide cancel/or not?
				case _PULSE_CODE_DISCONNECT:
					printf("[CrossServer: Info] PULSE_CODE_DISCONNECT received from client %d\n", recv.pulse.scoid);
					// Free any state kept with client

					// If scoid was not found in our established client list (from ADMIT)
					// just let it detach. might be due to failed connection or something
					if (ct != NULL)
					{
						if (ct->disconnect_handler != NULL)
						{
							ct->disconnect_handler((void *)&ct_d);
						}
						// else
						// {
						// 	// maybe a default one?
						// }

						// remove from dict and detach 
						/* 
							detach may fail but only on (coid doesnt exist) EINVAL
							which is fine. client details would be removed anyway

							closing of the reqs is the same.
							unless a timertimeout is used. 
							MsgError only has two errors: the waiting thread does not exist (rcvid) or etimedout (from timertimeout)

							if a timertimout is used, need to add further handling
						*/
						client_dict_remove(&client_dict, recv.pulse.scoid, CLIENT_DETACH);
					}
					else
					{
						ConnectDetach(recv.pulse.scoid);
					}
					break;
				case _PULSE_CODE_UNBLOCK:
				{
					printf("[CrossServer: Info] PULSE_CODE_UNBLOCK received from client %d\n", recv.pulse.scoid);
					
					int unblocked_rcvid = recv.pulse.value.sival_int;
					// just random idek
					if (ct != NULL)
					{
						// the unblock handler (see function down belowwww), it removes the req from the client's list
						ct_d.data = &unblocked_rcvid;
						if (ct->unblock_handler != NULL)
						{
							ct->unblock_handler((void *)&ct_d);
						}
						// else
						// {
						// 	// Default one?
						// }
					}
					// recv.pulse.value.sival_int holds the recv
					MsgError(unblocked_rcvid, EINTR);
					break;
				}
				// My codes. maybe use a my_codes func to make code more legible
				case CROSSING_CONTROLLER_CLIENT_ID:
				{
					switch (recv.pulse.value.sival_int)
					{
						case SELF_WAKE_PULSE:
						{
							//printf("[CrossServer: Info] Received a wakeup pulse from CROSSING_CONTROLLER\n");
							break;
						}
						default:
						{
							printf("[CrossServer: Warning] Received unknown pulse (value: %d) from CROSSING_CONTROLLER\n", recv.pulse.value.sival_int);
							break;
						}
					}
					break;
				}
				default:
					printf("[CrossServer: Warning] Received unknown pulse code %d. Value is %d\n", recv.pulse.code, recv.pulse.value.sival_int);
					break;
			}
			//continue; // go back to the top of the while loop
		}


		if (rcvid > 0)
		{
			// Message received
			printf("[CrossServer: Info] Received message (scoid: %d, type: %d, data: %d)\n", info.scoid, recv.msg.type, recv.msg.data);

			if (recv.msg.type == _IO_CONNECT)
			{
				if (MsgReply(rcvid, EOK, NULL, 0) == -1)
				{
					printf("[CrossServer: Warning] Failed to reply to _IO_CONNECT (rcvid %d): %s. Sending MsgError(EAGAIN)\n", rcvid, strerror(errno));
					MsgError(rcvid, EAGAIN);
					continue;
				}
				printf("[CrossServer: Info] Received _IO_CONNECT message, replied with EOK\n");
				continue;
			}
			else if (recv.msg.type > _IO_BASE && recv.msg.type <= _IO_MAX)
			{
				MsgError(rcvid, ENOSYS);
				printf("[CrossServer: Warning] Received IO message, rejected it (ENOSYS)\n");
				continue;
			}
			// need to add my own admitter. (separate form the IO_CONNECT>)
			else if (recv.msg.type == ADMITTER_CODE)
			{
				// client add or get will reset the client_details_t if update_if_exists = 1. includes cancelling all reqs
				client_dict_add_get_return_t r = client_dict_get_or_add(&client_dict, info.scoid, recv.msg.client_identifier, CLIENT_UPDATE_IF_EXISTS);
				if (r.ct == NULL)
				{
					MsgError(rcvid, EBUSY); // client_dict is full. reject connection
					printf("[CrossServer: Warning] Received ADMITTER message but client dict was full... replied with EBUSY\n");
					continue; // ?
				}
				if (MsgReply(rcvid, EOK, NULL, 0) == -1)
				{
					printf("[CrossServer: Warning] Failed to reply to ADMITTER (rcvid %d): %s. Sending MsgError(EAGAIN)\n", rcvid, strerror(errno));
					MsgError(rcvid, EAGAIN);
					continue;
				}
				printf("[CrossServer: Info] Received ADMITTER message, replied with EOK\n");
				continue;
			}
			else
			{
				if (_my_message_handler(&client_dict, &info, rcvid, &recv) == EXIT_FAILURE)
				{
					// If my handlers couldnt handle the message, then no func exists to deal with it
					MsgError(rcvid, ENOSYS);
				}
			}
			
		}

		// req processing
		_my_req_process(&client_dict, ev);
	}
}


/* 
	Replies EOK to the train for a finished req, then removes it from the client's reqs.
	If the reply fails, sends MsgError(err_code) instead so the client is not left blocked.
	Returns EXIT_SUCCESS if the reply was sent, EXIT_FAILURE if MsgError had to be used.
*/ 
static int _reply_req_done(client_details_t *ct, int rcvid, int err_code)
{
	int rc = EXIT_SUCCESS;
	reply_t rply = {.status = EOK, .err_msg = "", .data = 0};
	if (MsgReply(rcvid, EOK, &rply, sizeof(rply)) == -1)
	{
		printf("[CrossServer: Warning] Failed to reply to req (rcvid %d): %s. Sending MsgError(%d)\n", rcvid, strerror(errno), err_code);
		MsgError(rcvid, err_code);
		rc = EXIT_FAILURE;
	}

	reqs_remove(ct, rcvid);
	return rc;
}

/* 
	Replies STOP_TRAIN (status EFAULT) to the train to inform of crossing controller failures, then removes it from the client's reqs.
	If the reply fails, sends MsgError(err_code) instead so the client is not left blocked.
	Returns EXIT_SUCCESS if the reply was sent, EXIT_FAILURE if MsgError had to be used.
*/
static int _reply_req_fail(client_details_t *ct, int rcvid, int err_code)
{
	int rc = EXIT_SUCCESS;
	reply_t rply = {.status = EFAULT, .err_msg = "STOP_TRAIN", .data = 0};
	if (MsgReply(rcvid, EOK, &rply, sizeof(rply)) == -1)
	{
		printf("[CrossServer: Warning] Failed to reply to req (rcvid %d): %s. Sending MsgError(%d)\n", rcvid, strerror(errno), err_code);
		MsgError(rcvid, err_code);
		rc = EXIT_FAILURE;
	}

	reqs_remove(ct, rcvid);
	return rc;
}

/*
	Processes the requests found in client_details_t of client_dict_t.
	A finished req is replied to and removed.
	May reply with a fail or a success depending on... well how i coded it
*/
static int _my_req_process(client_dict_t *client_dict, events_t *ev)
{
	//int replied = 0;
	for(uint8_t i = 0; i < client_dict->count; i++)
	{
		client_details_t *ct = &client_dict->entries[i];
		req_array_t *reqs = &ct->reqs;
		switch (ct->client_id)
		{
			case TRAIN_CONTROLLER_CLIENT_ID:
			{
				// removing a req, moves the last req in the array into the same 
				// position, so need to process the same position of array again
				uint8_t j = 0;
				while (j < reqs->count)
				{
					req_t *req = &reqs->entries[j];
					if (req->type == APPROACH_NOTIFY)
					{
						if (req->state == REQ_STATE_INIT) // Notification not yet noted
						{
							// Need to consider fail points
							if (pthread_mutex_lock(&ev->mutex) == EOK)
							{
								// If the X1_FAULT event is on, then send error message to the train to stop
								if (ev->events[EV_X1_FAULT] == EV_X1_FAULT_STATE_ON)
								{
									pthread_mutex_unlock(&ev->mutex);

									_reply_req_fail(ct, req->rcvid, EFAULT);
									//replied++;
									continue;
								}
								// If it reached this point then the X1_FAULT is not on.
								// Set event to indicate it has occured
								ev->events[EV_TRAIN_APPROACH] = EV_TRAIN_APPROACH_STATE_CROSSING_NOTIFIED;
								// Signal the state controller
								pthread_cond_signal(&ev->cond);
								pthread_mutex_unlock(&ev->mutex);
								req->state = REQ_STATE_RUNNING_S1; // Train approach noted. State changed.
							}
						}
						if (req->state == REQ_STATE_RUNNING_S1) // Intersections to be notified
						{
							// stravation for other? the state_transitioner?
							if (pthread_mutex_lock(&ev->mutex) == EOK)
							{
								if (ev->events[EV_X1_FAULT] == EV_X1_FAULT_STATE_ON)
								{
									pthread_mutex_unlock(&ev->mutex);

									_reply_req_fail(ct, req->rcvid, EFAULT);
									//replied++;
									continue;
								}

								// If the event has progressed since first notification
								// send message to the intersections
								// depending on result of it, different states

								ev->events[EV_ROADS_CLEAR] = EV_ROADS_CLEAR_STATE_INTERSECTIONS_NOTIFIED;
								pthread_cond_broadcast(&ev->cond);
								pthread_mutex_unlock(&ev->mutex);
								req->state = REQ_STATE_RUNNING_S2; // Intersections notified. State changed
							}
						}

						if (req->state == REQ_STATE_RUNNING_S2) // Check if crossing state is GATES DOWN
						{
							// werid but easier than the other option.
							// use events[GATES_DOWN] as information for this
							uint8_t safe = 0;
							if (pthread_mutex_lock(&ev->mutex) == EOK)
							{
								if (ev->events[EV_X1_FAULT] == EV_X1_FAULT_STATE_ON)
								{
									pthread_mutex_unlock(&ev->mutex);

									_reply_req_fail(ct, req->rcvid, EFAULT);
									//replied++;
									continue;
								}

								if (ev->events[EV_GATE_DOWN] == EV_GATE_DOWN_STATE_DOWN)
								{
									safe = 1;
									ev->events[EV_GATE_DOWN] = 0; // reset it
								}
								pthread_mutex_unlock(&ev->mutex);
							}
							if (safe == 1)
							{
								req->state = REQ_STATE_DONE;
								_reply_req_done(ct, req->rcvid, EFAULT);
								//replied++;
								continue; // slot j now holds a different req
							}
						}
					}
					else if (req->type == CROSSING_NOTIFY)
					{
						if (req->state == REQ_STATE_INIT)
						{
							if (pthread_mutex_lock(&ev->mutex) == EOK)
							{
								if (ev->events[EV_X1_FAULT] == EV_X1_FAULT_STATE_ON)
								{
									pthread_mutex_unlock(&ev->mutex);

									_reply_req_fail(ct, req->rcvid, EFAULT);
									//replied++;
									continue;
								}

								ev->events[EV_TRAIN_CROSSING] = EV_TRAIN_CROSSING_STATE_CROSSING_NOTIFIED;
								pthread_cond_signal(&ev->cond);
								pthread_mutex_unlock(&ev->mutex);
								req->state = REQ_STATE_DONE;
								_reply_req_done(ct, req->rcvid, EFAULT);
								//replied++;
								continue; // slot j now holds a different req
							}
						}
					}
					else if (req->type == EXIT_NOTIFY)
					{
						if (req->state == REQ_STATE_INIT)
						{
							if (pthread_mutex_lock(&ev->mutex) == EOK)
							{
								if (ev->events[EV_X1_FAULT] == EV_X1_FAULT_STATE_ON)
								{
									pthread_mutex_unlock(&ev->mutex);

									_reply_req_fail(ct, req->rcvid, EFAULT);
									//replied++;
									continue;
								}

								ev->events[EV_TRAIN_EXIT] = EV_TRAIN_EXIT_STATE_CROSSING_NOTIFIED;
								pthread_cond_signal(&ev->cond);
								pthread_mutex_unlock(&ev->mutex);
								req->state	= REQ_STATE_DONE;
								_reply_req_done(ct, req->rcvid, EFAULT);
								//replied++;
								continue; // slot j now holds a different req
							}
						}
					}
					else if (req->type == FAULT_NOTIFY)
					{
						if (req->state == REQ_STATE_INIT)
						{
							if (pthread_mutex_lock(&ev->mutex) == EOK)
							{
								ev->events[EV_X1_FAULT] = EV_X1_FAULT_STATE_ON;
								pthread_cond_signal(&ev->cond);
								pthread_mutex_unlock(&ev->mutex);
								req->state = REQ_STATE_DONE;
								_reply_req_done(ct, req->rcvid, EFAULT);
								//replied++;
								continue;
							}
						}
					}
					j++;
				}
				break;
			}
			default:
				break;
		}
	}
	//return replied;
	return EXIT_SUCCESS;
}

// returns EXIT_SUCCESS if the message was handled. EXIT_FAILURE if it wasnt (i.e. end of the func was reached)
static int _my_message_handler(client_dict_t *client_dict, struct _msg_info *info, int rcvid, recv_t *recv)
{
	// if client_id has changed (scoid reused) we believe its the same client
	// but if they disconnected it should be removed in the disconnect pulse handling. so i wonder if this is ok
	client_details_t *ct = client_dict_lookup_scoid(client_dict, info->scoid);

	if (ct == NULL)
	{
		// Maybe should send something in the reply? Some data and not just a error?
		printf("[CrossServer: Warning] Client's details could not be found in the client dict. Returning EINVAL (request invalid, admit first)\n");
		MsgError(rcvid, EINVAL);
		// If not found in the list, then ignore this rcvid
		return EXIT_SUCCESS; // ???
	}

	// if at this point then client exists and valid

	if (ct->state == CLIENT_BASE_DETAILS_SET)
	{
		client_handler_initaliser(ct);
	}

	if (ct->client_id == TRAIN_CONTROLLER_CLIENT_ID)
	{
		if (recv->msg.type == APPROACH_NOTIFY || recv->msg.type == CROSSING_NOTIFY || recv->msg.type == EXIT_NOTIFY || recv->msg.type == FAULT_NOTIFY)
		{
			req_t rq = {.rcvid = rcvid, .replaceable = REQ_NOT_REPLACEABLE, .type = recv->msg.type, .subtype = recv->msg.subtype, .state = REQ_STATE_INIT};
			if (reqs_add(ct, REQ_ADD_REPLACE, rq) == REQ_BUFF_FULL)
			{
				reply_t rply = {.status = EAGAIN, .err_msg = "REQ_BUFF_FULL", .data = 0};
				// EOK indicates message was recieved by server, but they check .status = EAGAIN and .err_msg
				if (MsgReply(rcvid, EOK, &rply, sizeof(rply)) == -1)
				{
					printf("[CrossServer: Warning] Failed to reply to req (rcvid %d): %s. Sending MsgError(EFAULT)\n", rcvid, strerror(errno));
					MsgError(rcvid, EAGAIN);
				}
				printf("[CrossServer: Warning] Train sent request (type %d) but the request buffer was full. Returned EAGAIN.\n", recv->msg.type);
			}
			else
				printf("[CrossServer: Info] Train sent request (type %d), request saved.\n", recv->msg.type);
		
			return EXIT_SUCCESS;
		}
	}

	return EXIT_FAILURE;
}

static void _disconnect_handler(void *data)
{
	client_handler_data_t *_d = (client_handler_data_t*)data;
	client_details_t *ct = _d->ct;
	events_t *ev = (events_t*)_d->data;
	switch (ct->client_id)
	{
		case TRAIN_CONTROLLER_CLIENT_ID:
		{
			if (pthread_mutex_lock(&ev->mutex) == EOK)
			{
				ev->events[EV_X1_FAULT] = EV_X1_FAULT_STATE_ON;
				pthread_cond_signal(&ev->cond);
				pthread_mutex_unlock(&ev->mutex);
			}
			// need to worry about what happens if mutex lock fails
			break;
		}
		default:
			break;
	}
}

static void _unblock_handler(void *data)
{
	client_handler_data_t *d = (client_handler_data_t *)data;  
	// maybe should have more rules or maybe a struct for data
	reqs_remove(d->ct, *(int *)d->data);
}

static void client_handler_initaliser(client_details_t *ct)
{
	switch (ct->client_id)
	{
	case TRAIN_CONTROLLER_CLIENT_ID:
		ct->disconnect_handler = _disconnect_handler;
		ct->unblock_handler = _unblock_handler;
		ct->state = CLIENT_ALL_DETAILS_SET;
		break;
	default:
		printf("[CrossServer: Error] ClientID not recognised, could not set the disconnect and unblock handlers\n");
		break;
	}
}


// Need to handle lost updates.
void *subserver_central_messenger(void *arg)
{
	// Employee handling messages to central server asynchronously


	// What should happen if it fails to connect to central controller?
	// Timed reconnection?. Just die?
	int rc = establish_connection(&central_con_details);
	if (rc != EXIT_SUCCESS)
	{
		central_con_details.status = STATUS_FAILED;
		printf("[Central: Error] Failed to connect to the central controller.\nsubserver_central_messager thread is exiting\n");
		return (void *)EXIT_FAILURE;
	}

	printf("[Central: Info] Successfully established connection to the central controller.\n");

	int event_given = 0; // indicate if thread should sleep as there is no work
	int should_sleep = 0;
	int event = 0;
	uint32_t data = 0;

	// Is while 1 a good idea? should there be an exit strategy?
	/* failure handling:
		a fatal error from sending pulses (i.e. not a EAGAIN)
		stops the thread (because pulses no longer send)

		however, if its just a server queue is full (or pulse queue is empty)
		situation, sleeping helps.
	*/
	while (1)
	{
		event_given = 0; // indicate if thread should sleep as there is no work
		should_sleep = 0;
		event = 0;

		rc = read_from_log_buffer(&pulses_to_central, &data);
		if (rc == EOK)
		{
			event = data;
			event_given = 1;
		}
		else
		{
			printf("[Central: Warning] Failed to get pulses from pulses_to_central buffer due to mutex lock: %s\n", strerror(rc));
		}

		if (event_given)
		{
			rc = send_update_pulses(&central_con_details, event);
			if (rc != EOK)
			{
				if (rc == EAGAIN)
				{
					printf("[Central: Warning] Central controller's kernel had insufficient resources to enqueue pulse.\nThe update message is lost\n");
					should_sleep = 1;
				}
				else
				{
					central_con_details.status = STATUS_FAILED;
					printf("[Central: Error] send_update_pulses failed. The central controller messenger thread is exiting.\n");
					return (void *)EXIT_FAILURE;
				}
			}
		}

		if (should_sleep)
		{
			sleep(2); // No work to do, sleep for some seconds.
		}

	}



	return NULL;
}
