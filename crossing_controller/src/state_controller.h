#ifndef SRC_STATE_CONTROLLER_H_
#define SRC_STATE_CONTROLLER_H_

#include "message_handling.h"

// --------------------------- START Crossing States Definition ------------------------------- //
typedef enum {IDLE = 0, TRAIN_APPROACHING, WARNING_ACTIVE, GATES_LOWERING, GATES_DOWN, TRAIN_CROSSING,
	TRAIN_CLEAR_WAIT, GATES_RAISING, X1_CLEAR, X1_FAULT} state_t;
extern const char *state_to_string[];

extern state_t cur_state;
extern state_t next_state;
// --------------------------- END Crossing States Definition ------------------------------- //

// --------------------------- START Events Definition ------------------------------- //
// Indexes for events_t.events. EV_ = events
#define EV_TRAIN_APPROACH 0
#define EV_WARNINGS_ACTIVE 1
#define EV_GATES_LOWERING 2
#define EV_GATE_DOWN 3
#define EV_TRAIN_CROSSING 4
#define EV_TRAIN_EXIT 5
#define EV_GATE_RAISED 6
#define EV_X1_FAULT 7
#define EV_COUNT 8


#define EV_TRAIN_APPROACH_STATE_CROSSING_NOTIFIED 1
#define EV_WARNINGS_ACTIVE_STATE_INTERSECTIONS_NOTIFIED 1
#define EV_TRAIN_CROSSING_STATE_CROSSING_NOTIFIED 1
#define EV_TRAIN_EXIT_STATE_CROSSING_NOTIFIED 1
#define EV_GATE_DOWN_STATE_DOWN 1
#define EV_GATE_RAISED_STATE_RAISED 1
#define EV_X1_FAULT_STATE_ON 1

typedef struct
{
	int 			events[EV_COUNT];
	pthread_mutex_t mutex;
	pthread_cond_t 	cond;
} events_t;


// extern events_t ev; ///
// --------------------------- END Events Definition ------------------------------- //

struct crossing_gates_t;

typedef struct
{
	events_t *ev;
	struct crossing_gates_t *gates;
} state_transitioner_data_t;

void *state_transitioner(void *arg);
int state_transition_message(void);

#endif /* SRC_STATE_CONTROLLER_H_ */
