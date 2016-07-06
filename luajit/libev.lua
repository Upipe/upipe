local ffi = require("ffi")
ffi.cdef [[
enum {
    EV_UNDEF    = (int)0xFFFFFFFF,
    EV_NONE     =            0x00,
    EV_READ     =            0x01,
    EV_WRITE    =            0x02,
    EV__IOFDSET =            0x80,
    EV_IO       =         EV_READ,
    EV_TIMER    =      0x00000100,
    EV_PERIODIC =      0x00000200,
    EV_SIGNAL   =      0x00000400,
    EV_CHILD    =      0x00000800,
    EV_STAT     =      0x00001000,
    EV_IDLE     =      0x00002000,
    EV_PREPARE  =      0x00004000,
    EV_CHECK    =      0x00008000,
    EV_EMBED    =      0x00010000,
    EV_FORK     =      0x00020000,
    EV_CLEANUP  =      0x00040000,
    EV_ASYNC    =      0x00080000,
    EV_CUSTOM   =      0x01000000,
    EV_ERROR    = (int)0x80000000
};
typedef double ev_tstamp;
struct ev_watcher {
    int active;
    int pending;
    int priority;
    void *data;
    void (*cb)(struct ev_loop *, struct ev_watcher *, int);
};
typedef struct ev_watcher ev_watcher;
struct ev_prepare {
    int active;
    int pending;
    int priority;
    void *data;
    void (*cb)(struct ev_loop *, struct ev_prepare *, int);
};
typedef struct ev_prepare ev_prepare;
typedef int __sig_atomic_t;
typedef __sig_atomic_t sig_atomic_t;
struct ev_watcher_list {
    int active;
    int pending;
    int priority;
    void *data;
    void (*cb)(struct ev_loop *, struct ev_watcher_list *, int);
    struct ev_watcher_list *next;
};
typedef struct ev_watcher_list ev_watcher_list;
struct ev_io {
    int active;
    int pending;
    int priority;
    void *data;
    void (*cb)(struct ev_loop *, struct ev_io *, int);
    struct ev_watcher_list *next;
    int fd;
    int events;
};
typedef struct ev_io ev_io;
struct ev_watcher_time {
    int active;
    int pending;
    int priority;
    void *data;
    void (*cb)(struct ev_loop *, struct ev_watcher_time *, int);
    ev_tstamp at;
};
typedef struct ev_watcher_time ev_watcher_time;
struct ev_idle {
    int active;
    int pending;
    int priority;
    void *data;
    void (*cb)(struct ev_loop *, struct ev_idle *, int);
};
typedef struct ev_idle ev_idle;
struct ev_check {
    int active;
    int pending;
    int priority;
    void *data;
    void (*cb)(struct ev_loop *, struct ev_check *, int);
};
struct ev_fork {
    int active;
    int pending;
    int priority;
    void *data;
    void (*cb)(struct ev_loop *, struct ev_fork *, int);
};
struct ev_cleanup {
    int active;
    int pending;
    int priority;
    void *data;
    void (*cb)(struct ev_loop *, struct ev_cleanup *, int);
};
struct ev_async {
    int active;
    int pending;
    int priority;
    void *data;
    void (*cb)(struct ev_loop *, struct ev_async *, int);
    sig_atomic_t volatile sent;
};
typedef void (*ev_loop_callback)(struct ev_loop *);
void ev_invoke_pending(struct ev_loop *);
void ev_set_syserr_cb(void (*)(char const *));
void ev_set_allocator(void *(*)(void *, long int));
ev_tstamp ev_time(void);
ev_tstamp ev_now(struct ev_loop *);
void ev_sleep(ev_tstamp);
void ev_feed_event(struct ev_loop *, void *, int);
void ev_feed_fd_event(struct ev_loop *, int, int);
void ev_feed_signal(int);
void ev_feed_signal_event(struct ev_loop *, int);
int ev_version_major(void);
int ev_version_minor(void);
unsigned int ev_supported_backends(void);
unsigned int ev_recommended_backends(void);
unsigned int ev_embeddable_backends(void);
unsigned int ev_backend(struct ev_loop *);
unsigned int ev_iteration(struct ev_loop *);
unsigned int ev_depth(struct ev_loop *);
void ev_set_io_collect_interval(struct ev_loop *, ev_tstamp);
void ev_set_timeout_collect_interval(struct ev_loop *, ev_tstamp);
void ev_set_userdata(struct ev_loop *, void *);
void *ev_userdata(struct ev_loop *);
void ev_set_invoke_pending_cb(struct ev_loop *, ev_loop_callback);
void ev_set_loop_release_cb(struct ev_loop *, void (*)(struct ev_loop *), void (*)(struct ev_loop *));
struct ev_loop *ev_loop_new(unsigned int);
void ev_verify(struct ev_loop *);
void ev_loop_fork(struct ev_loop *);
void ev_invoke(struct ev_loop *, void *, int);
unsigned int ev_pending_count(struct ev_loop *);
void ev_break(struct ev_loop *, int);
void ev_ref(struct ev_loop *);
void ev_unref(struct ev_loop *);
void ev_now_update(struct ev_loop *);
void ev_suspend(struct ev_loop *);
void ev_resume(struct ev_loop *);
int ev_clear_pending(struct ev_loop *, void *);
void ev_io_start(struct ev_loop *, ev_io *);
void ev_io_stop(struct ev_loop *, ev_io *);
struct ev_timer {
    int active;
    int pending;
    int priority;
    void *data;
    void (*cb)(struct ev_loop *, struct ev_timer *, int);
    ev_tstamp at;
    ev_tstamp repeat;
};
typedef struct ev_timer ev_timer;
void ev_timer_start(struct ev_loop *, ev_timer *);
void ev_timer_stop(struct ev_loop *, ev_timer *);
void ev_timer_again(struct ev_loop *, ev_timer *);
ev_tstamp ev_timer_remaining(struct ev_loop *, ev_timer *);
struct ev_periodic {
    int active;
    int pending;
    int priority;
    void *data;
    void (*cb)(struct ev_loop *, struct ev_periodic *, int);
    ev_tstamp at;
    ev_tstamp offset;
    ev_tstamp interval;
    ev_tstamp (*reschedule_cb)(struct ev_periodic *, ev_tstamp);
};
typedef struct ev_periodic ev_periodic;
void ev_periodic_start(struct ev_loop *, ev_periodic *);
void ev_periodic_stop(struct ev_loop *, ev_periodic *);
int ev_run(struct ev_loop *, int);
void ev_periodic_again(struct ev_loop *, ev_periodic *);
struct ev_signal {
    int active;
    int pending;
    int priority;
    void *data;
    void (*cb)(struct ev_loop *, struct ev_signal *, int);
    struct ev_watcher_list *next;
    int signum;
};
typedef struct ev_signal ev_signal;
void ev_signal_start(struct ev_loop *, ev_signal *);
struct ev_loop *ev_default_loop(unsigned int);
void ev_signal_stop(struct ev_loop *, ev_signal *);
void ev_loop_destroy(struct ev_loop *);
struct ev_child {
    int active;
    int pending;
    int priority;
    void *data;
    void (*cb)(struct ev_loop *, struct ev_child *, int);
    struct ev_watcher_list *next;
    int flags;
    int pid;
    int rpid;
    int rstatus;
};
typedef struct ev_child ev_child;
void ev_child_start(struct ev_loop *, ev_child *);
void ev_child_stop(struct ev_loop *, ev_child *);
void ev_idle_start(struct ev_loop *, ev_idle *);
void ev_idle_stop(struct ev_loop *, ev_idle *);
void ev_prepare_start(struct ev_loop *, ev_prepare *);
void ev_prepare_stop(struct ev_loop *, ev_prepare *);
typedef struct ev_check ev_check;
void ev_check_start(struct ev_loop *, ev_check *);
void ev_check_stop(struct ev_loop *, ev_check *);
typedef struct ev_fork ev_fork;
typedef struct ev_cleanup ev_cleanup;
struct ev_embed {
    int active;
    int pending;
    int priority;
    void *data;
    void (*cb)(struct ev_loop *, struct ev_embed *, int);
    struct ev_loop *other;
    ev_io io;
    ev_prepare prepare;
    ev_check check;
    ev_timer timer;
    ev_periodic periodic;
    ev_idle idle;
    ev_fork fork;
    ev_cleanup cleanup;
};
typedef struct ev_embed ev_embed;
void ev_embed_sweep(struct ev_loop *, ev_embed *);
void ev_fork_start(struct ev_loop *, ev_fork *);
void ev_embed_start(struct ev_loop *, ev_embed *);
void ev_fork_stop(struct ev_loop *, ev_fork *);
void ev_embed_stop(struct ev_loop *, ev_embed *);
void ev_cleanup_start(struct ev_loop *, ev_cleanup *);
void ev_cleanup_stop(struct ev_loop *, ev_cleanup *);
typedef struct ev_async ev_async;
void ev_async_start(struct ev_loop *, ev_async *);
void ev_async_stop(struct ev_loop *, ev_async *);
void ev_async_send(struct ev_loop *, ev_async *);
void ev_once(struct ev_loop *, int, int, ev_tstamp, void (*)(int, void *), void *);
]]
return ffi.load("libev.so.4", true)
