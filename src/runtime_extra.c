/* Imports the shared runtime files do not own, with the offline, single-user contracts of runtime_services.c.
 * The libc start-up and service contracts follow Catherine: Full Body PC and MediEvil PC (runtime_extra.c). */
#include "runtime.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define ERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
#define E_SRCH 3
#define E_INVAL 22
#define HTTP_NETWORK ((int32_t)0x80431063)
#define DIALOG_UNAVAILABLE ((int32_t)0x80B80001)

static ABI int32_t ok_noop(void) { return 0; }
static ABI void *null_pointer(void) { return NULL; }
static ABI int32_t not_found(void) { return ERR(E_SRCH); }

/* ---- libc.prx start-up (SDK 7.00 and 9.50): thread-exit and sanitizer hooks the runtime does not need ---- */
typedef struct { int64_t tv_sec, tv_nsec; } GuestTimespec;
static ABI int32_t kernel_nanosleep(const GuestTimespec *request, GuestTimespec *remain) {
    if (!request) return ERR(E_INVAL);
#ifdef _WIN32
    compat_sleep_ns((uint64_t)request->tv_sec*1000000000u+(uint64_t)request->tv_nsec);
#else
    struct timespec t={(time_t)request->tv_sec,(long)request->tv_nsec};
    while (nanosleep(&t,&t) && errno==EINTR) {}
#endif
    if (remain) remain->tv_sec=remain->tv_nsec=0;
    return 0;
}
static ABI int32_t thread_name(void *thread, char *name) {
    if (!name) return ERR(E_INVAL);
    const char *n=runtime_thread_name(thread);
    if (!n) return ERR(E_SRCH);
    snprintf(name,32,"%s",n);
    return 0;
}
static ABI int32_t kernel_chmod(const char *path, uint16_t mode) { (void)path; (void)mode; return 0; }
static ABI int32_t kernel_utimes(const char *path, const void *times) { (void)path; (void)times; return 0; }

/* ---- POSIX rwlocks: errno results, not SCE codes ---- */
typedef int32_t (ABI *RwOne)(void **);
static int32_t posix_code(int32_t r) { return r ? (int32_t)((uint32_t)r & 0xffff) : 0; }
#define POSIX_RW(name,nid) static ABI int32_t posix_rw_##name(void **rw) { \
    return posix_code(((RwOne)runtime_rwlock_resolve(nid))(rw)); }
POSIX_RW(rdlock,"Ox9i0c7L5w0#libkernel")
POSIX_RW(wrlock,"mqdNorrB+gI#libkernel")
POSIX_RW(unlock,"+L98PIbGttk#libkernel")

/* ---- Dialogs the offline console never shows: opening fails, status stays idle ---- */
static ABI int32_t dialog_unavailable(void) { return DIALOG_UNAVAILABLE; }
static ABI int32_t dialog_none(void) { return 0; }

/* ---- HTTP without a network: settings are accepted, nothing can be read back ---- */
static ABI int32_t http_network(void) { return HTTP_NETWORK; }

static const RuntimeExport exports[]={
    {"_nanosleep",kernel_nanosleep}, {"scePthreadGetname",thread_name},
    {"sceKernelChmod",kernel_chmod}, {"sceKernelUtimes",kernel_utimes},
    {"_sceKernelRtldThreadAtexitIncrement",ok_noop}, {"_sceKernelRtldThreadAtexitDecrement",ok_noop},
    {"_sceKernelSetThreadAtexitCount",ok_noop}, {"_sceKernelSetThreadAtexitReport",ok_noop},
    {"_sceKernelSetThreadDtors",ok_noop},
    {"sceKernelIsAddressSanitizerEnabled",ok_noop},
    {"sceKernelGetSanitizerMallocReplaceExternal",null_pointer},
    {"sceKernelGetSanitizerNewReplaceExternal",null_pointer},
    {"sceKernelInternalMemoryGetModuleSegmentInfo",not_found},
    {"sceSysmoduleGetModuleInfoForUnwind",not_found},
    {"pthread_rwlock_rdlock",posix_rw_rdlock}, {"pthread_rwlock_wrlock",posix_rw_wrlock},
    {"pthread_rwlock_unlock",posix_rw_unlock},
    {"sceInvitationDialogInitialize",ok_noop}, {"sceInvitationDialogTerminate",ok_noop},
    {"sceInvitationDialogOpenA",dialog_unavailable}, {"sceInvitationDialogUpdateStatus",dialog_none},
    {"sceInvitationDialogGetResultA",dialog_unavailable},
    {"sceGameCustomDataDialogInitialize",ok_noop}, {"sceGameCustomDataDialogTerminate",ok_noop},
    {"sceGameCustomDataDialogOpenA",dialog_unavailable}, {"sceGameCustomDataDialogUpdateStatus",dialog_none},
    {"sceGameCustomDataDialogGetResultA",dialog_unavailable},
    {"sceSigninDialogOpen",dialog_unavailable}, {"sceSigninDialogUpdateStatus",dialog_none},
    {"sceSigninDialogGetResult",dialog_unavailable},
    {"sceNpProfileDialogOpenA",dialog_unavailable}, {"sceNpCommerceDialogGetResult",dialog_unavailable},
    {"sceNpCommerceShowPsStoreIcon",ok_noop}, {"sceNpCommerceHidePsStoreIcon",ok_noop},
    {"sceNpTrophyShowTrophyList",ok_noop}, {"sceNpTrophyCaptureScreenshot",ok_noop},
    {"sceHttpSetAuthEnabled",ok_noop}, {"sceHttpSetAuthInfoCallback",ok_noop}, {"sceHttpSetCookieEnabled",ok_noop},
    {"sceHttpSetRecvTimeOut",ok_noop}, {"sceHttpSetResolveRetry",ok_noop}, {"sceHttpSetResolveTimeOut",ok_noop},
    {"sceHttpSetSendTimeOut",ok_noop}, {"sceHttpsSetSslCallback",ok_noop},
    {"sceHttpGetAllResponseHeaders",http_network}, {"sceHttpGetMemoryPoolStats",http_network},
    {"sceNpRegisterStateCallbackA",ok_noop}, {"sceNpUnregisterStateCallbackA",ok_noop},
    /* Console features with no PC counterpart: accepted and ignored. */
    {"sceGameLiveStreamingEnableLiveStreaming",ok_noop}, {"sceVideoRecordingSetInfo",ok_noop},
    {"sceShareUtilityAdvanceInitialize",ok_noop}, {"sceShareUtilityAdvanceTerminate",ok_noop},
    {"sceShareUtilityAdvanceSetUploadContentData",ok_noop},
};
uintptr_t runtime_extra_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
