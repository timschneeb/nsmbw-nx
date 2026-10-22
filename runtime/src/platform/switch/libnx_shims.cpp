// libnx leaves newlib's thread hooks unset, and both defaults are wrong for a
// recomp host: detach returns ENOSYS (std::thread::detach then throws) and an
// attribute-less pthread gets a 128 KiB stack, which Dawn's tint resolver
// overflows while parsing WGSL.

#include <switch.h>

#include <pthread.h>
#include <threads.h>

namespace {
constexpr size_t kDefaultThreadStackSize = 4 * 1024 * 1024;
}

// newlib's pthread_detach calls this weak hook and reports ENOSYS when unset, so
// std::thread::detach() throws. Detached host threads run for the whole session,
// so reporting success without reclaiming is correct.
extern "C" int __syscall_thread_detach(struct __pthread_t*) { return 0; }

extern "C" int __real_pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);

extern "C" int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                                     void* (*start)(void*), void* arg) {
    if (attr == nullptr) {
        pthread_attr_t sized;
        if (pthread_attr_init(&sized) != 0) {
            return __real_pthread_create(thread, nullptr, start, arg);
        }
        pthread_attr_setstacksize(&sized, kDefaultThreadStackSize);
        const int result = __real_pthread_create(thread, &sized, start, arg);
        pthread_attr_destroy(&sized);
        return result;
    }

    size_t stackSize = 0;
    if (pthread_attr_getstacksize(attr, &stackSize) == 0 && stackSize >= kDefaultThreadStackSize) {
        return __real_pthread_create(thread, attr, start, arg);
    }
    pthread_attr_t sized = *attr;
    if (pthread_attr_setstacksize(&sized, kDefaultThreadStackSize) != 0) {
        return __real_pthread_create(thread, attr, start, arg);
    }
    return __real_pthread_create(thread, &sized, start, arg);
}

extern "C" int __real_thrd_create(thrd_t*, thrd_start_t, void*);

// newlib reports success as thrd_success (4) while most libraries, Mesa
// included, test for 0 and abandon the thread on anything else.
extern "C" int __wrap_thrd_create(thrd_t* thread, thrd_start_t func, void* arg) {
    const int result = __real_thrd_create(thread, func, arg);
    return result == thrd_success ? 0 : result;
}
