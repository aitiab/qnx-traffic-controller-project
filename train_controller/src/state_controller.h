#ifndef SRC_STATE_CONTROLLER_H_
#define SRC_STATE_CONTROLLER_H_

#include "message_handling.h"

// --------------------------- START Train States Definition ------------------------------- //
typedef enum {NRML = 0, SYS_FAIL, APRCH, CROSS} state_t;
extern const char *state_to_string[];

extern state_t cur_state;
extern state_t next_state;
// --------------------------- END Train States Definition ------------------------------- //

// --------------------------- START Events Definition ------------------------------- //
typedef enum {DEFAULT = 0, APRCHNG, EMERG, CROSSNG, PAST, FIXED, CRITICAL_FAILURE} events_t;
// --------------------------- END Events Definition ------------------------------- //

// Miliseconds Timeouts for notifications to the crossing server. Adjust as needed
// MS_NOTIFY_APPROACH should cover how long a approch reply can take
// Crossing only replies to APPROACH_NOTIFY once the crossing's gates are down
// so must consider time to relay message to crossing servers' processing delays, intersections replys + gate lowering),
// CROSSING/EXIT are replied to straight away
#define MS_NOTIFY_APPROACH 5000
#define MS_NOTIFY_CROSSING 1000
#define MS_NOTIFY_EXIT     1000
// FAULT_NOTIFY is replied to straight away. Kept short so the train reaches SYS_FAIL quickly even if the crossing is unresponsive
#define MS_NOTIFY_FAULT 1000

int state_transitioner(events_t *ev, server_con_details_t *crossing_details);

#endif /* SRC_STATE_CONTROLLER_H_ */
