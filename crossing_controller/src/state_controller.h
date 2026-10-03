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
#define EV_COUNT 7

typedef struct
{
	int 			events[EV_COUNT];
	pthread_mutex_t mutex;
	pthread_cond_t 	cond;
} events_t;


// extern events_t ev; ///
// --------------------------- END Events Definition ------------------------------- //

void *state_transitioner(void *arg);
void activate_flashers(void);
void gates_down(void);
void gates_up(void);

#endif /* SRC_STATE_CONTROLLER_H_ */
