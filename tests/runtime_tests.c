/* Guest threads, mutexes and condition variables through their libkernel exports, as the guest calls them: creation,
 * exit, join and detach, handle lifecycle, the live count, the hang report's wait states. No game files, no window. */
#include "runtime_thread.c"
#include <stdatomic.h>

#define KERR(n) ((int32_t)(UINT32_C(0x80020000) | (n)))

static int checks, failures;
#define CHECK(condition) do { __atomic_add_fetch(&checks, 1, __ATOMIC_RELAXED); if (!(condition)) { __atomic_add_fetch(&failures, 1, __ATOMIC_RELAXED); printf("FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

void runtime_thread_keys_cleanup(void) {}

typedef int32_t (ABI *CreateFn)(GuestThread **, ThreadAttr **, GuestEntry, void *, const char *);
typedef int32_t (ABI *JoinFn)(GuestThread *, void **);
typedef int32_t (ABI *HandleFn)(GuestThread *);
typedef void (ABI *ExitFn)(void *);
typedef int32_t (ABI *AttrFn)(ThreadAttr **);
typedef int32_t (ABI *AttrDetachFn)(ThreadAttr **, int);
typedef int32_t (ABI *MutexInitFn)(void **, void **, const char *);
typedef int32_t (ABI *MutexAttrTypeFn)(void **, int);
typedef int32_t (ABI *MutexFn)(void **);
typedef int32_t (ABI *CondInitFn)(void **, void **, const char *);
typedef int32_t (ABI *CondWaitFn)(void **, void **);
typedef int32_t (ABI *CondTimedWaitFn)(void **, void **, uint32_t);
typedef int32_t (ABI *CondFn)(void **);

static void *thread_export(const char *nid) {
    void *f = (void *)runtime_thread_resolve(nid);
    if (!f) { printf("FAIL: thread export %s is missing\n", nid); exit(1); }
    return f;
}
static void *mutex_export(const char *nid) {
    void *f = (void *)runtime_mutex_resolve(nid);
    if (!f) { printf("FAIL: mutex export %s is missing\n", nid); exit(1); }
    return f;
}

static CreateFn create_thread;
static JoinFn join_thread;
static HandleFn detach_thread;
static ExitFn exit_thread;
static AttrFn attr_create, attr_free;
static AttrDetachFn attr_detach;
static MutexInitFn mutex_create;
static MutexAttrTypeFn mutex_attr_type;
static MutexFn mutex_attr_create;
static MutexFn mutex_acquire, mutex_try, mutex_release, mutex_free;
static CondInitFn cond_create;
static CondWaitFn cond_block;
static CondTimedWaitFn cond_block_timed;
static CondFn cond_wake, cond_wake_all, cond_free;

static size_t live_threads(void) {
    pthread_mutex_lock(&lock);
    size_t live = created - exited;
    pthread_mutex_unlock(&lock);
    return live;
}
static void wait_until(atomic_int *flag, int value) {
    for (int i = 0; i < 5000 && atomic_load(flag) != value; ++i) sched_yield(), compat_sleep_ns(1000000);
}
static const char *wait_state_after(GuestThread *t, const char *expected) {
    const char *what = NULL;
    for (int i = 0; i < 2000; ++i) {
        what = runtime_thread_wait_state(t, NULL);
        if (what && !strcmp(what, expected)) break;
        compat_sleep_ns(1000000);
    }
    return what;
}

static ABI void *return_argument(void *argument) { return (void *)((uintptr_t)argument * 2); }
static ABI void *exit_early(void *argument) {
    exit_thread((void *)((uintptr_t)argument + 1));
    return NULL;
}
static atomic_int detached_ran;
static ABI void *mark_detached(void *argument) { (void)argument; atomic_fetch_add(&detached_ran, 1); return NULL; }

static void test_create_join_exit(void) {
    size_t before = live_threads();
    GuestThread *t = NULL;
    void *result = NULL;
    CHECK(create_thread(&t, NULL, return_argument, (void *)21, "join-me") == 0);
    CHECK(t != NULL && !strcmp(runtime_thread_name(t), "join-me"));
    CHECK(join_thread(t, &result) == 0);
    CHECK((uintptr_t)result == 42);
    CHECK(join_thread(t, &result) == KERR(22));
    CHECK(detach_thread(t) == KERR(22));

    CHECK(create_thread(&t, NULL, exit_early, (void *)9, NULL) == 0);
    CHECK(!strcmp(runtime_thread_name(t), "guest"));
    CHECK(join_thread(t, &result) == 0);
    CHECK((uintptr_t)result == 10);
    CHECK(live_threads() == before);

    CHECK(create_thread(&t, NULL, NULL, NULL, "no entry") == KERR(22));
    CHECK(create_thread(NULL, NULL, return_argument, NULL, "no handle") == KERR(22));
    CHECK(join_thread((GuestThread *)&before, &result) == KERR(3));
    CHECK(join_thread(runtime_thread_current(), &result) == KERR(3));
}

static void test_detached_threads(void) {
    size_t before = live_threads();
    ThreadAttr *attr = NULL;
    CHECK(attr_create(&attr) == 0);
    CHECK(attr_detach(&attr, 1) == 0);
    atomic_store(&detached_ran, 0);
    for (int i = 0; i < 64; ++i) {
        GuestThread *t = NULL;
        CHECK(create_thread(&t, &attr, mark_detached, NULL, "detached") == 0);
        void *result;
        CHECK(join_thread(t, &result) == KERR(22));
    }
    wait_until(&detached_ran, 64);
    CHECK(atomic_load(&detached_ran) == 64);
    for (int i = 0; i < 2000 && live_threads() != before; ++i) compat_sleep_ns(1000000);
    CHECK(live_threads() == before);
    CHECK(attr_free(&attr) == 0);
    CHECK(attr == NULL);

    GuestThread *t = NULL;
    CHECK(create_thread(&t, NULL, return_argument, (void *)1, "detach-later") == 0);
    CHECK(detach_thread(t) == 0);
    CHECK(detach_thread(t) == KERR(22));
    for (int i = 0; i < 2000 && live_threads() != before; ++i) compat_sleep_ns(1000000);
    CHECK(live_threads() == before);
}

static void *shared_mutex, *shared_cond;
static atomic_int stage;
static ABI void *hold_mutex(void *argument) {
    (void)argument;
    mutex_acquire(&shared_mutex);
    atomic_store(&stage, 1);
    wait_until(&stage, 2);
    mutex_release(&shared_mutex);
    return NULL;
}
static ABI void *lock_once(void *argument) {
    (void)argument;
    mutex_acquire(&shared_mutex);
    mutex_release(&shared_mutex);
    return NULL;
}
static ABI void *wait_on_cond(void *argument) {
    (void)argument;
    mutex_acquire(&shared_mutex);
    atomic_store(&stage, 1);
    while (atomic_load(&stage) != 2) cond_block(&shared_cond, &shared_mutex);
    mutex_release(&shared_mutex);
    return (void *)7;
}

static void test_mutexes_and_wait_states(void) {
    void *attr = NULL, *recursive = NULL;
    CHECK(mutex_create(&shared_mutex, NULL, "shared") == 0);
    CHECK(mutex_acquire(&shared_mutex) == 0);
    CHECK(mutex_acquire(&shared_mutex) == KERR(11));
    CHECK(mutex_try(&shared_mutex) == KERR(16));
    CHECK(mutex_release(&shared_mutex) == 0);
    CHECK(mutex_release(&shared_mutex) == KERR(1));

    CHECK(mutex_attr_create(&attr) == 0);
    CHECK(mutex_attr_type(&attr, 2) == 0);
    CHECK(mutex_create(&recursive, &attr, "recursive") == 0);
    CHECK(mutex_acquire(&recursive) == 0 && mutex_acquire(&recursive) == 0);
    CHECK(mutex_release(&recursive) == 0 && mutex_release(&recursive) == 0);
    CHECK(mutex_free(&recursive) == 0);

    /* A contended lock shows as "mutex" while it waits and clears once it is taken. */
    GuestThread *holder = NULL, *waiter = NULL;
    atomic_store(&stage, 0);
    CHECK(create_thread(&holder, NULL, hold_mutex, NULL, "holder") == 0);
    wait_until(&stage, 1);
    CHECK(create_thread(&waiter, NULL, lock_once, NULL, "waiter") == 0);
    const char *what = wait_state_after(waiter, "mutex");
    CHECK(what && !strcmp(what, "mutex"));
    uint64_t waited = 0;
    CHECK(runtime_thread_wait_state(waiter, &waited) && waited < 60000000);
    atomic_store(&stage, 2);
    void *result;
    CHECK(join_thread(holder, &result) == 0);
    CHECK(join_thread(waiter, &result) == 0);
    CHECK(runtime_thread_wait_state(waiter, NULL) == NULL);

    CHECK(cond_create(&shared_cond, NULL, "cond") == 0);
    atomic_store(&stage, 0);
    CHECK(create_thread(&waiter, NULL, wait_on_cond, NULL, "cond-waiter") == 0);
    what = wait_state_after(waiter, "condition variable");
    CHECK(what && !strcmp(what, "condition variable"));
    mutex_acquire(&shared_mutex);
    atomic_store(&stage, 2);
    CHECK(cond_wake_all(&shared_cond) == 0);
    mutex_release(&shared_mutex);
    CHECK(join_thread(waiter, &result) == 0 && (uintptr_t)result == 7);

    mutex_acquire(&shared_mutex);
    CHECK(cond_block_timed(&shared_cond, &shared_mutex, 2000) == KERR(60));
    CHECK(runtime_thread_wait_state(runtime_thread_current(), NULL) == NULL);
    mutex_release(&shared_mutex);
    CHECK(cond_wake(&shared_cond) == 0);
    CHECK(cond_free(&shared_cond) == 0);
    CHECK(mutex_free(&shared_mutex) == 0);
}

static void test_report(void) {
    GuestThread *t = NULL;
    void *result;
    CHECK(create_thread(&t, NULL, return_argument, NULL, "reported") == 0);
    CHECK(join_thread(t, &result) == 0);
    runtime_thread_wait_begin("sleep");
    runtime_thread_report();
    runtime_thread_wait_end();
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    runtime_thread_attach_main();
    create_thread = thread_export("6UgtwV+0zb4#libkernel");
    join_thread = thread_export("onNY9Byn-W8#libkernel");
    detach_thread = thread_export("4qGrR6eoP9Y#libkernel");
    exit_thread = thread_export("3kg7rT0NQIs#libkernel");
    attr_create = thread_export("nsYoNRywwNg#libkernel");
    attr_free = thread_export("62KCwEMmzcM#libkernel");
    attr_detach = thread_export("-Wreprtu0Qs#libkernel");
    mutex_attr_create = mutex_export("F8bUHwAG284#libkernel");
    mutex_attr_type = mutex_export("iMp8QpE+XO4#libkernel");
    mutex_create = mutex_export("cmo1RIYva9o#libkernel");
    mutex_acquire = mutex_export("9UK1vLZQft4#libkernel");
    mutex_try = mutex_export("upoVrzMHFeE#libkernel");
    mutex_release = mutex_export("tn3VlD0hG60#libkernel");
    mutex_free = mutex_export("2Of0f+3mhhE#libkernel");
    cond_create = mutex_export("2Tb92quprl0#libkernel");
    cond_block = mutex_export("WKAXJ4XBPQ4#libkernel");
    cond_block_timed = mutex_export("BmMjYxmew1w#libkernel");
    cond_wake = mutex_export("kDh-NfxgMtE#libkernel");
    cond_wake_all = mutex_export("JGgj7Uvrl+A#libkernel");
    cond_free = mutex_export("g+PZd2hiacg#libkernel");

    test_create_join_exit();
    test_detached_threads();
    test_mutexes_and_wait_states();
    test_report();
    printf("runtime: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
