#ifndef SRC_MESSAGE_CONTROLLER_H_
#define SRC_MESSAGE_CONTROLLER_H_

#include "message_handling.h"

#define QNET_CENTRAL_CONTROLLER_ATTACH_POINT "/net/VM_x86_Target01/dev/name/local/central_controller"
#define TRAIN_CONTROLLER_ATTACH_POINT "train_controller"

extern log_buffer_t pulses_to_central;
extern server_con_details_t central_con_details;

void *subserver_central_messenger(void *arg);

#endif /* SRC_MESSAGE_CONTROLLER_H_ */
