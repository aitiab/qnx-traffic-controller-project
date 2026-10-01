#ifndef SRC_INPUT_H_
#define SRC_INPUT_H_

#include <pthread.h>
#include <stdint.h>

#include "state_controller.h"

// Size of the events circular buffer where input/sensor data goes
#define EVENT_BUFF_SIZE 10

typedef struct {
	pthread_mutex_t mutex;
	pthread_cond_t 	cond;
	uint8_t 		count;
	events_t 		events[EVENT_BUFF_SIZE];
	uint8_t 		nextRead;
	uint8_t 		nextWrite;
	uint8_t 		status;
} input_t;

extern input_t input_obj;

void *terminal_in(void *arg);
void readInput(events_t *ev);

#endif /* SRC_INPUT_H_ */
