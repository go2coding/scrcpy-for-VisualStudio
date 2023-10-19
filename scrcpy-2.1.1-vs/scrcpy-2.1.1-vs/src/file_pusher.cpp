#include "file_pusher.h"

#include <assert.h>
#include <string.h>

#include "adb/adb.h"
#include "util/log.h"
#include "util/process_intr.h"

#define DEFAULT_PUSH_TARGET "/sdcard/Download/"

static void
sc_file_pusher_request_destroy(struct sc_file_pusher_request *req) {
    free(req->file);
}

bool
sc_file_pusher_init(struct sc_file_pusher *fp, const char *serial,
                    const char *push_target) {
    assert(serial);

    //sc_vecdeque_init(&fp->queue);

    (&fp->queue)->cap = 0; 
    (&fp->queue)->origin = 0; 
    (&fp->queue)->size = 0;
    (&fp->queue)->data = NULL;

    bool ok = sc_mutex_init(&fp->mutex);
    if (!ok) {
        return false;
    }

    ok = sc_cond_init(&fp->event_cond);
    if (!ok) {
        sc_mutex_destroy(&fp->mutex);
        return false;
    }

    ok = sc_intr_init(&fp->intr);
    if (!ok) {
        sc_cond_destroy(&fp->event_cond);
        sc_mutex_destroy(&fp->mutex);
        return false;
    }

    fp->serial = strdup(serial);
    if (!fp->serial) {
        LOG_OOM();
        sc_intr_destroy(&fp->intr);
        sc_cond_destroy(&fp->event_cond);
        sc_mutex_destroy(&fp->mutex);
        return false;
    }

    // lazy initialization
    fp->initialized = false;

    fp->stopped = false;

    fp->push_target = push_target ? push_target : DEFAULT_PUSH_TARGET;

    return true;
}

void
sc_file_pusher_destroy(struct sc_file_pusher *fp) {
    sc_cond_destroy(&fp->event_cond);
    sc_mutex_destroy(&fp->mutex);
    sc_intr_destroy(&fp->intr);
    free(fp->serial);

    while (!sc_vecdeque_is_empty(&fp->queue)) {
        //struct sc_file_pusher_request *req = sc_vecdeque_popref(&fp->queue);
        struct sc_file_pusher_request* req;
        assert(!sc_vecdeque_is_empty(&fp->queue));
            size_t pos = (&fp->queue)->origin;
            (&fp->queue)->origin = ((&fp->queue)->origin + 1) % (&fp->queue)->cap; 
            --(&fp->queue)->size;
            req = & (&fp->queue)->data[pos];

//=================
        assert(req);
        sc_file_pusher_request_destroy(req);
    }
}

bool
sc_file_pusher_request(struct sc_file_pusher *fp,
                       enum sc_file_pusher_action action, char *file) {
    // start file_pusher if it's used for the first time
    if (!fp->initialized) {
        if (!sc_file_pusher_start(fp)) {
            return false;
        }
        fp->initialized = true;
    }

    LOGI("Request to %s %s", action == SC_FILE_PUSHER_ACTION_INSTALL_APK
                                 ? "install" : "push",
                             file);
    struct sc_file_pusher_request req = {
        .action = action,
        .file = file,
    };

    sc_mutex_lock(&fp->mutex);
    bool was_empty = sc_vecdeque_is_empty(&fp->queue);
    //bool res = sc_vecdeque_push(&fp->queue, req);
    bool res = false;
    //bool ok = sc_vecdeque_grow_if_needed_(pv);
    //(!sc_vecdeque_is_full(pv) || sc_vecdeque_grow_(pv))
    if (!sc_vecdeque_is_full(&fp->queue)) {
        if ((&fp->queue)->cap < sc_vecdeque_max_cap_(&fp->queue)) {
            size_t newsize = sc_vecdeque_growsize_((&fp->queue)->cap);
            newsize = CLAMP(newsize, SC_VECDEQUE_MINCAP_,
                sc_vecdeque_max_cap_(&fp->queue));
            //ok = sc_vecdeque_realloc_(&db->queue, newsize);
            void* p = sc_vecdeque_reallocdata_((&fp->queue)->data, newsize, \
                sizeof(*(&fp->queue)->data), &(&fp->queue)->cap, \
                & (&fp->queue)->origin, (&fp->queue)->size);
            if (p) {
                (&fp->queue)->data = (struct sc_file_pusher_request*) p;
            }
            res = (bool)p;
        }
        else {

            res = false;
        }
    }
    else
    {
        res = false;
    }
    if (res) {
        //sc_vecdeque_push_noresize(&fp->queue, req);
        assert(!sc_vecdeque_is_full(&fp->queue));
        ++(&fp->queue)->size;
        (&fp->queue)->data[((&fp->queue)->origin + (&fp->queue)->size - 1) % (&fp->queue)->cap] = req;
    }

    //========


    if (!res) {
        LOG_OOM();
        sc_mutex_unlock(&fp->mutex);
        return false;
    }

    if (was_empty) {
        sc_cond_signal(&fp->event_cond);
    }
    sc_mutex_unlock(&fp->mutex);

    return true;
}

static int
run_file_pusher(void *data) {
    struct sc_file_pusher *fp = (struct sc_file_pusher*)data;
    struct sc_intr *intr = &fp->intr;

    const char *serial = fp->serial;
    assert(serial);

    const char *push_target = fp->push_target;
    assert(push_target);

    for (;;) {
        sc_mutex_lock(&fp->mutex);
        while (!fp->stopped && sc_vecdeque_is_empty(&fp->queue)) {
            sc_cond_wait(&fp->event_cond, &fp->mutex);
        }
        if (fp->stopped) {
            // stop immediately, do not process further events
            sc_mutex_unlock(&fp->mutex);
            break;
        }

        assert(!sc_vecdeque_is_empty(&fp->queue));
        //struct sc_file_pusher_request req = sc_vecdeque_pop(&fp->queue);
        struct sc_file_pusher_request req;
        assert(!sc_vecdeque_is_empty(&fp->queue)); 
            size_t pos = (&fp->queue)->origin; 
            (&fp->queue)->origin = ((&fp->queue)->origin + 1) % (&fp->queue)->cap; 
            --(&fp->queue)->size; 
            req = (&fp->queue)->data[pos]; 
            ///===============
        sc_mutex_unlock(&fp->mutex);

        if (req.action == SC_FILE_PUSHER_ACTION_INSTALL_APK) {
            LOGI("Installing %s...", req.file);
            bool ok = sc_adb_install(intr, serial, req.file, 0);
            if (ok) {
                LOGI("%s successfully installed", req.file);
            } else {
                LOGE("Failed to install %s", req.file);
            }
        } else {
            LOGI("Pushing %s...", req.file);
            bool ok = sc_adb_push(intr, serial, req.file, push_target, 0);
            if (ok) {
                LOGI("%s successfully pushed to %s", req.file, push_target);
            } else {
                LOGE("Failed to push %s to %s", req.file, push_target);
            }
        }

        sc_file_pusher_request_destroy(&req);
    }
    return 0;
}

bool
sc_file_pusher_start(struct sc_file_pusher *fp) {
    LOGD("Starting file_pusher thread");

    bool ok = sc_thread_create(&fp->thread, run_file_pusher, "scrcpy-file", fp);
    if (!ok) {
        LOGE("Could not start file_pusher thread");
        return false;
    }

    return true;
}

void
sc_file_pusher_stop(struct sc_file_pusher *fp) {
    if (fp->initialized) {
        sc_mutex_lock(&fp->mutex);
        fp->stopped = true;
        sc_cond_signal(&fp->event_cond);
        sc_intr_interrupt(&fp->intr);
        sc_mutex_unlock(&fp->mutex);
    }
}

void
sc_file_pusher_join(struct sc_file_pusher *fp) {
    if (fp->initialized) {
        sc_thread_join(&fp->thread, NULL);
    }
}
