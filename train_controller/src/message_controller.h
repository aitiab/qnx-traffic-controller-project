#ifndef SRC_MESSAGE_CONTROLLER_H_
#define SRC_MESSAGE_CONTROLLER_H_

#include "message_handling.h"

#define QNET_CENTRAL_CONTROLLER_ATTACH_POINT "/net/VM_x86_Target01/dev/name/local/central_controller"
#define CROSSING_SERVER_ATTACH_POINT "crossing_controller"

#define ADMITTER_CODE = 999 // should confirm this is valid.

extern log_buffer_t pulses_to_central;
extern server_con_details_t central_con_details;

void *subserver_central_messenger(void *arg);

#endif /* SRC_MESSAGE_CONTROLLER_H_ */
