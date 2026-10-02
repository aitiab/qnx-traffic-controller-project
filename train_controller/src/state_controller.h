#ifndef SRC_STATE_CONTROLLER_H_
#define SRC_STATE_CONTROLLER_H_

// --------------------------- START Train States Definition ------------------------------- //
typedef enum {NRML = 0, SYS_FAIL, APRCH, EM_B_CROSS, CROSS, EM_A_CROSS} state_t;
extern const char *state_to_string[];

extern state_t cur_state;
extern state_t next_state;
// --------------------------- END Train States Definition ------------------------------- //

// --------------------------- START Events Definition ------------------------------- //
typedef enum {DEFAULT = 0, APRCHNG, EMERG, CROSSNG, PAST, FIXED, CRITICAL_FAILURE} events_t;
// --------------------------- END Events Definition ------------------------------- //

int state_transitioner(events_t *ev);

#endif /* SRC_STATE_CONTROLLER_H_ */
