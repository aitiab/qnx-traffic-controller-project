/*
 * central_debug.c - debug stand-in for the central controller.
 *
 * Attaches as "central_controller" and prints every pulse and message it
 * receives from train controllers. Pulses arrive with code = the train's
 * client_identifier and value = from_state + to_state * 10.
 *
 * Console commands (type while running):
 *   ok          reply EOK with no data to messages (default)
 *   err <n>     reply MsgError(n) to messages, e.g. "err 22"
 *   echo        reply with a copy of the received message
 *   quit        exit
 *
 * Build: qcc -Vgcc_ntox86_64 -o central_debug central_debug.c
 * Run it on the node the train expects (VM_x86_Target01).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>
#include <sys/iomsg.h>

#define ATTACH_NAME   "central_controller"
#define MAX_MSG_SIZE  512

typedef union {
    uint16_t      type;
    struct _pulse pulse;
    uint8_t       raw[MAX_MSG_SIZE];
} recv_buf_t;

enum { REPLY_OK = 0, REPLY_ERR, REPLY_ECHO };

static atomic_int reply_mode  = REPLY_OK;
static atomic_int reply_errno = EINVAL;

/* Must match state_t in the train controller */
static const char *state_names[] = {
    "NORMAL", "SYSTEM_FAILURE", "APPROACHING_CROSSING",
    "EMERGENCY_BEFORE_CROSSING", "CROSSING",
    "EMERGENCY_AT_CROSSING"
};
#define NUM_STATES ((int)(sizeof(state_names) / sizeof(state_names[0])))

static void timestamp(void)
{
    struct timespec ts;
    struct tm tm;

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    printf("[%02d:%02d:%02d.%03ld] ", tm.tm_hour, tm.tm_min, tm.tm_sec,
           ts.tv_nsec / 1000000);
}

static void hex_dump(const uint8_t *data, size_t len)
{
    size_t shown = len > 64 ? 64 : len;

    printf("    ");
    for (size_t i = 0; i < shown; i++) {
        printf("%02x ", data[i]);
        if (i % 16 == 15 && i + 1 < shown)
            printf("\n    ");
    }
    if (shown < len)
        printf("... (%zu more bytes)", len - shown);
    printf("\n");
}

static void print_state_update(int value)
{
    int from = value % 10;
    int to   = value / 10;

    if (value >= 0 && from < NUM_STATES && to < NUM_STATES)
        printf("    state update: (%d) %s -> (%d) %s\n",
               from, state_names[from], to, state_names[to]);
    else
        printf("    (value doesn't decode as a state update)\n");
}

static void *console(void *arg)
{
    char line[64];
    int n;
    (void)arg;

    printf("Commands: ok | err <errno> | echo | quit\n");

    while (fgets(line, sizeof line, stdin) != NULL) {
        line[strcspn(line, "\n")] = '\0';

        if (strcasecmp(line, "ok") == 0) {
            atomic_store(&reply_mode, REPLY_OK);
            printf("Reply mode: EOK\n");
        } else if (sscanf(line, "err %d", &n) == 1) {
            atomic_store(&reply_errno, n);
            atomic_store(&reply_mode, REPLY_ERR);
            printf("Reply mode: MsgError(%d) %s\n", n, strerror(n));
        } else if (strcasecmp(line, "echo") == 0) {
            atomic_store(&reply_mode, REPLY_ECHO);
            printf("Reply mode: echo\n");
        } else if (strcasecmp(line, "quit") == 0) {
            break;
        } else if (line[0] != '\0') {
            printf("Unknown command: %s\n", line);
        }
    }

    /* Process exit also removes the name from /dev/name/local */
    printf("Exiting\n");
    exit(EXIT_SUCCESS);
    return NULL;
}

int main(void)
{
    name_attach_t *attach;
    pthread_t tid;
    recv_buf_t msg;
    struct _msg_info info;
    int rc;

    attach = name_attach(NULL, ATTACH_NAME, 0);
    if (attach == NULL) {
        perror("name_attach");
        return EXIT_FAILURE;
    }
    printf("Central debug server attached as /dev/name/local/%s (chid %d)\n",
           ATTACH_NAME, attach->chid);

    rc = pthread_create(&tid, NULL, console, NULL);
    if (rc != EOK) {
        printf("pthread_create: %s\n", strerror(rc));
        name_detach(attach, 0);
        return EXIT_FAILURE;
    }

    while (1) {
        int rcvid = MsgReceive(attach->chid, &msg, sizeof msg, &info);

        if (rcvid == -1) {
            perror("MsgReceive");
            continue;
        }

        /* ---------------- Pulses ---------------- */
        if (rcvid == 0) {
            timestamp();
            switch (msg.pulse.code) {
            case _PULSE_CODE_DISCONNECT:
                printf("Client disconnected (scoid %d)\n", msg.pulse.scoid);
                ConnectDetach(msg.pulse.scoid);
                break;
            case _PULSE_CODE_UNBLOCK:
                printf("Client requested unblock (rcvid %d)\n",
                       msg.pulse.value.sival_int);
                break;
            default:
                /* Train sends pulse code = client_identifier, value = state update */
                printf("PULSE from client %d: value=%d (scoid %d)\n",
                       msg.pulse.code, msg.pulse.value.sival_int,
                       msg.pulse.scoid);
                print_state_update(msg.pulse.value.sival_int);
                break;
            }
            continue;
        }

        /* ---------------- Messages ---------------- */

        /* name_open() sends an _IO_CONNECT; it must get EOK back */
        if (msg.type == _IO_CONNECT) {
            timestamp();
            printf("Client connected: pid %d, node %u\n",
                   (int)info.pid, (unsigned)info.nd);
            MsgReply(rcvid, EOK, NULL, 0);
            continue;
        }

        /* Other resource-manager messages aren't supported */
        if (msg.type > _IO_BASE && msg.type <= _IO_MAX) {
            MsgError(rcvid, ENOSYS);
            continue;
        }

        timestamp();
        printf("MESSAGE from pid %d node %u: type=%u, %lu bytes%s\n",
               (int)info.pid, (unsigned)info.nd, (unsigned)msg.type,
               (unsigned long)info.msglen,
               info.msglen >= sizeof msg ? " (may be truncated)" : "");
        hex_dump(msg.raw, info.msglen);

        switch (atomic_load(&reply_mode)) {
        case REPLY_ERR: {
            int e = atomic_load(&reply_errno);
            MsgError(rcvid, e);
            printf("    -> replied MsgError(%d) %s\n", e, strerror(e));
            break;
        }
        case REPLY_ECHO:
            MsgReply(rcvid, EOK, &msg, info.msglen);
            printf("    -> echoed %lu bytes\n", (unsigned long)info.msglen);
            break;
        default:
            MsgReply(rcvid, EOK, NULL, 0);
            printf("    -> replied EOK\n");
            break;
        }
    }

    name_detach(attach, 0);
    return EXIT_SUCCESS;
}
