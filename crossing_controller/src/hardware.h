#ifndef SRC_HARDWARE_H_
#define SRC_HARDWARE_H_

#include <stdint.h>
#include <pthread.h>

#include "state_controller.h"

// --------------------------- START Crossing Gates Definition ------------------------------- //
#define GATES_NOT_SET 	0
#define GATES_LOWER		1
#define GATES_RAISE		2

typedef struct crossing_gates_t
{
	uint8_t cur_state;
	uint8_t next_state;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
} crossing_gates_t;

typedef struct
{
	crossing_gates_t *gates;
	events_t *ev;
} crossing_gates_controller_data_t;
// --------------------------- END Crossing Gates Definition ------------------------------- //

void *crossing_gates_controller(void *arg);
int activate_flashers(void);
int deactivate_flashers(void);
int gates_req(crossing_gates_t *gates, uint8_t next_state);

#endif /* SRC_HARDWARE_H_ */
