#ifndef SRC_MESSAGE_CONTROLLER_H_
#define SRC_MESSAGE_CONTROLLER_H_

#include "message_handling.h"
#include "state_controller.h"
#include <sys/iomsg.h>
#include <semaphore.h>

#define QNET_CENTRAL_CONTROLLER_ATTACH_POINT "/net/VM_x86_Target01/dev/name/local/central_controller"
#define CROSSING_SERVER_ATTACH_POINT "crossing_controller"

// Must match the train controller's message_controller.h
#define ADMITTER_CODE (_IO_MAX + 1) // should confirm this is valid.
#define CROSSING_NOTIFY (_IO_MAX + 2)
#define APPROACH_NOTIFY (_IO_MAX + 3)
#define EXIT_NOTIFY (_IO_MAX + 4)
#define FAULT_NOTIFY (_IO_MAX + 5)

#define SELF_WAKE_PULSE 0

#define TRAIN_CONTROLLER_CLIENT_ID (_PULSE_CODE_MINAVAIL + 51) // also confirm this
#define CROSSING_CONTROLLER_CLIENT_ID   (_PULSE_CODE_MINAVAIL + 50)

typedef struct {
    events_t *ev;
    sem_t *sem;
} server_crossing_controller_data;

extern server_create_details_t crossing_server_details;
extern server_con_details_t self_con_details;

// State transition messages queued here are pulsed to the central controller by subserver_central_messenger
extern log_buffer_t pulses_to_central;
extern server_con_details_t central_con_details;

void *server_crossing_controller(void *arg);
void *subserver_central_messenger(void *arg);

#endif /* SRC_MESSAGE_CONTROLLER_H_ */
