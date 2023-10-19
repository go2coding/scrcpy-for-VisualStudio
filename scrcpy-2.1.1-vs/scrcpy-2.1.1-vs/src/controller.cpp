#include "controller.h"

#include <assert.h>

#include "util/log.h"

#define SC_CONTROL_MSG_QUEUE_MAX 64

bool
sc_controller_init(struct sc_controller *controller, sc_socket control_socket,
                   struct sc_acksync *acksync) {
    //sc_vecdeque_init(&controller->queue);
    (&controller->queue)->cap = 0; 
    (&controller->queue)->origin = 0; 
    (&controller->queue)->size = 0; 
    (&controller->queue)->data = NULL; 

    //bool ok = sc_vecdeque_reserve(&controller->queue, SC_CONTROL_MSG_QUEUE_MAX);
    size_t mincap = SC_CONTROL_MSG_QUEUE_MAX;
    assert(mincap <= sc_vecdeque_max_cap_(&controller->queue));
    bool ok;
    /* avoid to allocate tiny arrays (< SC_VECDEQUE_MINCAP_) */
    size_t mincap_ = MAX(mincap, SC_VECDEQUE_MINCAP_);
        if (mincap_ <= (&controller->queue)->cap) {
                /* nothing to do */
                ok = true; 
        }
        else if (mincap_ <= sc_vecdeque_max_cap_(&controller->queue)) {
                /* not too big */
                size_t newsize = sc_vecdeque_growsize_((&controller->queue)->cap);
                newsize = CLAMP(newsize, mincap_, sc_vecdeque_max_cap_(&controller->queue));
                //ok = sc_vecdeque_realloc_(&controller->queue, newsize);
                void* p = sc_vecdeque_reallocdata_((&controller->queue)->data, newsize, \
                    sizeof(*(&controller->queue)->data), &(&controller->queue)->cap, \
                    & (&controller->queue)->origin, (&controller->queue)->size);
                    if (p) {

                            (&controller->queue)->data = (sc_control_msg*)p;
                    } 
                    ok = (bool)p;
        }
        else {
                ok = false; 
        }


    if (!ok) {
        return false;
    }

    ok = sc_receiver_init(&controller->receiver, control_socket, acksync);
    if (!ok) {
        sc_vecdeque_destroy(&controller->queue);
        return false;
    }

    ok = sc_mutex_init(&controller->mutex);
    if (!ok) {
        sc_receiver_destroy(&controller->receiver);
        sc_vecdeque_destroy(&controller->queue);
        return false;
    }

    ok = sc_cond_init(&controller->msg_cond);
    if (!ok) {
        sc_receiver_destroy(&controller->receiver);
        sc_mutex_destroy(&controller->mutex);
        sc_vecdeque_destroy(&controller->queue);
        return false;
    }

    controller->control_socket = control_socket;
    controller->stopped = false;

    return true;
}

void
sc_controller_destroy(struct sc_controller *controller) {
    sc_cond_destroy(&controller->msg_cond);
    sc_mutex_destroy(&controller->mutex);

    while (!sc_vecdeque_is_empty(&controller->queue)) {
        //struct sc_control_msg *msg = sc_vecdeque_popref(&controller->queue);
        struct sc_control_msg* msg;
        assert(!sc_vecdeque_is_empty(&controller->queue)); 
        size_t pos = (&controller->queue)->origin;
            (&controller->queue)->origin = ((&controller->queue)->origin + 1) % (&controller->queue)->cap;
            --(&controller->queue)->size;
            msg = & (&controller->queue)->data[pos];
//============
        assert(msg);
        sc_control_msg_destroy(msg);
    }
    sc_vecdeque_destroy(&controller->queue);

    sc_receiver_destroy(&controller->receiver);
}

bool
sc_controller_push_msg(struct sc_controller *controller,
                       const struct sc_control_msg *msg) {
    if (sc_get_log_level() <= SC_LOG_LEVEL_VERBOSE) {
        sc_control_msg_log(msg);
    }

    sc_mutex_lock(&controller->mutex);
    bool full = sc_vecdeque_is_full(&controller->queue);
    if (!full) {
        bool was_empty = sc_vecdeque_is_empty(&controller->queue);
        //sc_vecdeque_push_noresize(&controller->queue, *msg);
        assert(!sc_vecdeque_is_full(&controller->queue));
        ++(&controller->queue)->size;
        (&controller->queue)->data[((&controller->queue)->origin + (&controller->queue)->size - 1) % (&controller->queue)->cap] = *msg;
        //==============

        if (was_empty) {
            sc_cond_signal(&controller->msg_cond);
        }
    }
    // Otherwise (if the queue is full), the msg is discarded

    sc_mutex_unlock(&controller->mutex);

    return !full;
}

static bool
process_msg(struct sc_controller *controller,
            const struct sc_control_msg *msg) {
    static unsigned char serialized_msg[SC_CONTROL_MSG_MAX_SIZE];
    size_t length = sc_control_msg_serialize(msg, serialized_msg);
    if (!length) {
        return false;
    }
    ssize_t w =
        net_send_all(controller->control_socket, serialized_msg, length);
    return (size_t) w == length;
}

static int
run_controller(void *data) {
    struct sc_controller *controller = (struct sc_controller *)data;

    for (;;) {
        sc_mutex_lock(&controller->mutex);
        while (!controller->stopped
                && sc_vecdeque_is_empty(&controller->queue)) {
            sc_cond_wait(&controller->msg_cond, &controller->mutex);
        }
        if (controller->stopped) {
            // stop immediately, do not process further msgs
            sc_mutex_unlock(&controller->mutex);
            break;
        }

        assert(!sc_vecdeque_is_empty(&controller->queue));
        //struct sc_control_msg msg = sc_vecdeque_pop(&controller->queue);
        struct sc_control_msg msg;
        assert(!sc_vecdeque_is_empty(&controller->queue));
            size_t pos = (&controller->queue)->origin;
            (&controller->queue)->origin = ((&controller->queue)->origin + 1) % (&controller->queue)->cap;
            --(&controller->queue)->size;
            msg = (&controller->queue)->data[pos];
//=================

        sc_mutex_unlock(&controller->mutex);

        bool ok = process_msg(controller, &msg);
        sc_control_msg_destroy(&msg);
        if (!ok) {
            LOGD("Could not write msg to socket");
            break;
        }
    }
    return 0;
}

bool
sc_controller_start(struct sc_controller *controller) {
    LOGD("Starting controller thread");

    bool ok = sc_thread_create(&controller->thread, run_controller,
                               "scrcpy-ctl", controller);
    if (!ok) {
        LOGE("Could not start controller thread");
        return false;
    }

    if (!sc_receiver_start(&controller->receiver)) {
        sc_controller_stop(controller);
        sc_thread_join(&controller->thread, NULL);
        return false;
    }

    return true;
}

void
sc_controller_stop(struct sc_controller *controller) {
    sc_mutex_lock(&controller->mutex);
    controller->stopped = true;
    sc_cond_signal(&controller->msg_cond);
    sc_mutex_unlock(&controller->mutex);
}

void
sc_controller_join(struct sc_controller *controller) {
    sc_thread_join(&controller->thread, NULL);
    sc_receiver_join(&controller->receiver);
}
