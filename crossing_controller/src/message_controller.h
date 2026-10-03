#ifndef SRC_MESSAGE_CONTROLLER_H_
#define SRC_MESSAGE_CONTROLLER_H_

#include "message_handling.h"
#include <sys/iomsg.h>

#define CROSSING_SERVER_ATTACH_POINT "crossing_controller"

// Must match the train controller's message_controller.h
#define ADMITTER_CODE (_IO_MAX + 1) // should confirm this is valid.
#define CROSSING_NOTIFY (_IO_MAX + 2)

#define TRAIN_CONTROLLER_CLIENT_ID (101) // also confirm this

extern server_create_details_t crossing_server_details;

void *server_crossing_controller(void *arg);

#endif /* SRC_MESSAGE_CONTROLLER_H_ */
