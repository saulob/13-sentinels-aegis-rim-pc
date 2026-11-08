/* Save data through the real save and file layers, called by export name as the guest calls them, in a scratch user
 * folder. The optional user-save replay works on a copy; the original is hashed before and after. */
#include "runtime_savedata.c"
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define GUEST_RDONLY 0
#define GUEST_WRONLY 1
#define GUEST_RDWR 2
#define GUEST_CREAT 0x200
#define GUEST_TRUNC 0x400
#define GUEST_DIRECTORY 0x20000
#define KERNEL_ENOENT ((int64_t)(int32_t)0x80020002)
#define KERNEL_EROFS ((int64_t)(int32_t)0x8002001e)
#define MOUNT_RDWR 2

static int checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    for (size_t i = 0; i < count; ++i) if (!strcmp(table[i].name, name)) return (uintptr_t)table[i].function;
    return 0;
}
int32_t runtime_guest_errno(int e) {
    switch (e) {
    case ENOENT: return 2; case EBADF: return 9; case EACCES: return 13; case EEXIST: return 17;
    case ENOTDIR: return 20; case EISDIR: return 21; case EINVAL: return 22; case EMFILE: return 24; case EROFS: return 30;
    default: return 5;
    }
}
static int32_t guest_errno_value;
int32_t *runtime_errno(void) { return &guest_errno_value; }

typedef int32_t (ABI *InitializeFn)(const void *);
typedef int32_t (ABI *Mount2Fn)(const Mount2 *, MountResult *);
typedef int32_t (ABI *UmountFn)(const MountPoint *);
typedef int32_t (ABI *MountInfoFn)(const MountPoint *, SearchInfo *);
typedef int32_t (ABI *SetParamFn)(const MountPoint *, uint32_t, const void *, uint64_t);
typedef int32_t (ABI *SearchFn)(const SearchCond *, SearchResult *);
typedef int32_t (ABI *DeleteFn)(const Delete *);
typedef int32_t (ABI *MemorySetupFn)(int32_t, uint64_t, const Param *);
typedef int32_t (ABI *MemoryIoFn)(int32_t, void *, uint64_t, int64_t);
typedef int64_t (ABI *OpenFn)(const char *, int, int);
typedef int64_t (ABI *CloseFn)(int);
typedef int64_t (ABI *ReadFn)(int, void *, uint64_t);
typedef int64_t (ABI *PreadFn)(int, void *, uint64_t, int64_t);
typedef int64_t (ABI *WriteFn)(int, const void *, uint64_t);
typedef int64_t (ABI *LseekFn)(int, int64_t, int);
typedef int64_t (ABI *FstatFn)(int, void *);
typedef int64_t (ABI *GetdentsFn)(int, char *, uint64_t, int64_t *);
typedef int64_t (ABI *RenameFn)(const char *, const char *);
typedef int64_t (ABI *UnlinkFn)(const char *);

static struct {
    InitializeFn init; Mount2Fn mount2; UmountFn umount; MountInfoFn info; SetParamFn set_param; SearchFn search;
    DeleteFn remove; MemorySetupFn memory_setup; MemoryIoFn memory_get, memory_set;
    OpenFn open; CloseFn close; ReadFn read; PreadFn pread; WriteFn write; LseekFn lseek; FstatFn fstat;
    GetdentsFn getdents; RenameFn rename; UnlinkFn unlink;
} api;

static void *save_export(const char *name) {
    void *f = (void *)runtime_savedata_resolve(name);
    if (!f) { printf("FAIL: save data export %s is missing\n", name); exit(1); }
    return f;
}
static void *file_export(const char *name) {
    void *f = (void *)runtime_file_resolve(name);
    if (!f) { printf("FAIL: file export %s is missing\n", name); exit(1); }
    return f;
}
static void resolve_api(void) {
    api.init = save_export("sceSaveDataInitialize3");
    api.mount2 = save_export("sceSaveDataMount2");
    api.umount = save_export("sceSaveDataUmount");
    api.info = save_export("sceSaveDataGetMountInfo");
    api.set_param = save_export("sceSaveDataSetParam");
    api.search = save_export("sceSaveDataDirNameSearch");
    api.remove = save_export("sceSaveDataDelete");
    api.memory_setup = save_export("sceSaveDataSetupSaveDataMemory");
    api.memory_get = save_export("sceSaveDataGetSaveDataMemory");
    api.memory_set = save_export("sceSaveDataSetSaveDataMemory");
    api.open = file_export("sceKernelOpen");
    api.close = file_export("sceKernelClose");
    api.read = file_export("sceKernelRead");
    api.pread = file_export("sceKernelPread");
    api.write = file_export("sceKernelWrite");
    api.lseek = file_export("sceKernelLseek");
    api.fstat = file_export("sceKernelFstat");
    api.getdents = file_export("sceKernelGetdirentries");
    api.rename = file_export("sceKernelRename");
    api.unlink = file_export("sceKernelUnlink");
}

static char test_root[700], user_root[760], save_root[800];

static void wipe(const char *path) {
    struct stat st;
    if (stat(path, &st)) return;
    if (!S_ISDIR(st.st_mode)) { unlink(path); return; }
    DIR *d = opendir(path);
    for (struct dirent *e; d && (e = readdir(d));) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char child[1200];
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        wipe(child);
    }
    if (d) closedir(d);
    rmdir(path);
}
static int exists(const char *path) { struct stat st; return !stat(path, &st); }
static int64_t host_size(const char *path) { struct stat st; return stat(path, &st) ? -1 : (int64_t)st.st_size; }

static DirName dir_name(const char *name) { DirName d; memset(&d, 0, sizeof(d)); snprintf(d.data, sizeof(d.data), "%s", name); return d; }
static int32_t mount_dir(const char *name, uint32_t mode, MountResult *result) {
    DirName dir = dir_name(name);
    Mount2 m; memset(&m, 0, sizeof(m));
    m.user = 1; m.dir = &dir; m.blocks = 1024; m.mode = mode;
    memset(result, 0, sizeof(*result));
    return api.mount2(&m, result);
}
static int64_t write_file(const char *guest, const void *data, size_t size, int flags) {
    int64_t fd = api.open(guest, flags, 0644);
    if (fd < 0) return fd;
    int64_t n = api.write((int)fd, data, size);
    api.close((int)fd);
    return n;
}
/* FNV-1a over a host file: proves the user's original save is unchanged. */
static uint64_t file_hash(const char *path, int64_t *size) {
    FILE *f = fopen(path, "rb");
    uint64_t h = UINT64_C(1469598103934665603);
    *size = 0;
    if (!f) return 0;
    unsigned char buffer[65536];
    for (size_t n; (n = fread(buffer, 1, sizeof(buffer), f)) > 0;) {
        for (size_t i = 0; i < n; ++i) { h ^= buffer[i]; h *= UINT64_C(1099511628211); }
        *size += (int64_t)n;
    }
    fclose(f);
    return h;
}
static int copy_file(const char *from, const char *to) {
    FILE *in = fopen(from, "rb"), *out = in ? fopen(to, "wb") : NULL;
    unsigned char buffer[65536];
    int ok = in && out;
    for (size_t n; ok && (n = fread(buffer, 1, sizeof(buffer), in)) > 0;) ok = fwrite(buffer, 1, n, out) == n;
    if (in) fclose(in);
    if (out && fclose(out)) ok = 0;
    return ok;
}

static void test_requires_initialize(void) {
    MountResult r;
    CHECK(mount_dir("SAVEDATA00", MODE_CREATE | MOUNT_RDWR, &r) == ERR_NOT_INITIALIZED);
    CHECK(api.init(NULL) == 0);
}

static void test_missing_and_invalid(void) {
    MountResult r;
    CHECK(mount_dir("NOSUCHSAVE", MOUNT_RDWR, &r) == ERR_NOT_FOUND);
    CHECK(mount_dir("NOSUCHSAVE", MODE_RDONLY, &r) == ERR_NOT_FOUND);
    const char *bad[] = {"", "../escape", "a/b", "a\\b", "..", "x.."};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) CHECK(mount_dir(bad[i], MODE_CREATE | MOUNT_RDWR, &r) == ERR_PARAMETER);
    Mount2 m; memset(&m, 0, sizeof(m));
    CHECK(api.mount2(&m, &r) == ERR_PARAMETER);
    DirName unterminated; memset(unterminated.data, 'A', sizeof(unterminated.data));
    m.user = 1; m.dir = &unterminated; m.mode = MODE_CREATE | MOUNT_RDWR;
    CHECK(api.mount2(&m, &r) == ERR_PARAMETER);
    m.dir = NULL;
    CHECK(api.mount2(NULL, &r) == ERR_PARAMETER);
    DirName dir = dir_name("SAVEDATA00");
    m.user = -1; m.dir = &dir; m.mode = MODE_CREATE;
    CHECK(api.mount2(&m, &r) == ERR_INVALID_USER);
    MountPoint nowhere; memset(&nowhere, 0, sizeof(nowhere)); snprintf(nowhere.data, sizeof(nowhere.data), "/savedata9");
    CHECK(api.umount(&nowhere) == ERR_NOT_FOUND);
    CHECK(api.umount(NULL) == ERR_NOT_FOUND);
    SearchInfo info;
    CHECK(api.info(&nowhere, &info) == ERR_NOT_MOUNTED);
}

static void test_create_write_and_reload(void) {
    MountResult r;
    CHECK(mount_dir("SAVEDATA00", MODE_CREATE | MOUNT_RDWR, &r) == 0);
    CHECK(!strcmp(r.point.data, "/savedata0"));
    CHECK(r.status == 1);
    char host[900], meta[900];
    snprintf(host, sizeof(host), "%s/SAVEDATA00", save_root);
    snprintf(meta, sizeof(meta), "%s/SAVEDATA00.sce_sys/param.bin", save_root);
    CHECK(exists(host));
    CHECK(host_size(meta) == (int64_t)sizeof(Param));

    unsigned char data[100000];
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = (unsigned char)(i * 7 + 3);
    CHECK(write_file("/savedata0/ROBO13.DAT", data, sizeof(data), GUEST_WRONLY | GUEST_CREAT | GUEST_TRUNC) == (int64_t)sizeof(data));
    CHECK(api.set_param(&r.point, 1, "Chapter 1", 9) == 0);
    SearchInfo info;
    CHECK(api.info(&r.point, &info) == 0);
    CHECK(info.blocks >= 4 && info.free_blocks <= info.blocks);

    MountResult again;
    CHECK(mount_dir("SAVEDATA00", MOUNT_RDWR, &again) == ERR_BAD_MOUNTED);
    CHECK(mount_dir("SAVEDATA00", MODE_CREATE | MOUNT_RDWR, &again) == ERR_EXISTS);
    CHECK(api.umount(&r.point) == 0);
    CHECK(api.umount(&r.point) == ERR_NOT_FOUND);
    CHECK(api.open("/savedata0/ROBO13.DAT", GUEST_RDONLY, 0) == KERNEL_ENOENT);

    CHECK(mount_dir("SAVEDATA00", MODE_RDONLY, &r) == 0);
    CHECK(r.status == 0);
    CHECK(!strcmp(r.point.data, "/savedata0"));
    int64_t fd = api.open("/savedata0/ROBO13.DAT", GUEST_RDONLY, 0);
    CHECK(fd >= 3);
    unsigned char stat_buffer[120];
    CHECK(api.fstat((int)fd, stat_buffer) == 0);
    int64_t size; memcpy(&size, stat_buffer + 72, 8);
    CHECK(size == (int64_t)sizeof(data));
    CHECK(api.lseek((int)fd, 0, 2) == (int64_t)sizeof(data));
    CHECK(api.lseek((int)fd, 4096, 0) == 4096);
    unsigned char back[sizeof(data)];
    CHECK(api.read((int)fd, back, 1000) == 1000);
    CHECK(!memcmp(back, data + 4096, 1000));
    CHECK(api.lseek((int)fd, 0, 1) == 5096);
    CHECK(api.pread((int)fd, back, sizeof(back), 0) == (int64_t)sizeof(data));
    CHECK(!memcmp(back, data, sizeof(data)));
    CHECK(api.lseek((int)fd, 0, 1) == 5096);
    CHECK(api.pread((int)fd, back, 16, (int64_t)sizeof(data)) == 0);
    CHECK(api.close((int)fd) == 0);
    CHECK(api.close((int)fd) < 0);

    Param p;
    CHECK(runtime_savedata_param(1, "SAVEDATA00", &p) == 0);
    CHECK(!strcmp(p.title, "Chapter 1"));
    CHECK(api.umount(&r.point) == 0);
}

static void test_directory_listing(void) {
    MountResult r;
    CHECK(mount_dir("SAVEDATA00", MODE_RDONLY, &r) == 0);
    int64_t fd = api.open("/savedata0", GUEST_RDONLY | GUEST_DIRECTORY, 0);
    CHECK(fd >= 3);
    char entries[4096]; int64_t base = -1;
    int64_t n = fd >= 3 ? api.getdents((int)fd, entries, sizeof(entries), &base) : -1;
    int found = 0;
    for (int64_t at = 0; n > 0 && at < n;) {
        uint16_t reclen; memcpy(&reclen, entries + at + 4, 2);
        if (!reclen) break;
        if ((unsigned char)entries[at + 7] == 10 && !memcmp(entries + at + 8, "ROBO13.DAT", 10)) found = entries[at + 6] == 8;
        at += reclen;
    }
    CHECK(base == 0);
    CHECK(found);
    if (fd >= 3) api.close((int)fd);
    CHECK(api.umount(&r.point) == 0);
}

/* Each cycle mounts at the same point and takes the same descriptor: nothing accumulates. */
static void test_repeated_mounts(void) {
    int first_fd = -1, stable = 1;
    size_t mounts_before = mounts_done, unmounts_before = unmounts_done;
    for (int i = 0; i < 200; ++i) {
        MountResult r;
        if (mount_dir("SAVEDATA00", i % 2 ? MODE_RDONLY : MOUNT_RDWR, &r) || strcmp(r.point.data, "/savedata0")) { stable = 0; break; }
        int64_t fd = api.open("/savedata0/ROBO13.DAT", GUEST_RDONLY, 0);
        unsigned char byte;
        if (fd < 3 || api.pread((int)fd, &byte, 1, 0) != 1) stable = 0;
        if (first_fd < 0) first_fd = (int)fd;
        if (fd != first_fd) stable = 0;
        api.close((int)fd);
        if (api.umount(&r.point)) stable = 0;
    }
    CHECK(stable);
    CHECK(mounts_done - mounts_before == 200);
    CHECK(unmounts_done - unmounts_before == 200);
}

static void test_slots_full(void) {
    MountResult r[SLOTS + 1];
    int created = 1;
    for (int i = 0; i < SLOTS; ++i) {
        char name[32]; snprintf(name, sizeof(name), "SLOT%02d", i);
        if (mount_dir(name, MODE_CREATE | MOUNT_RDWR, &r[i])) created = 0;
    }
    CHECK(created);
    CHECK(mount_dir("SLOTEXTRA", MODE_CREATE | MOUNT_RDWR, &r[SLOTS]) == ERR_MOUNT_FULL);
    char extra[900]; snprintf(extra, sizeof(extra), "%s/SLOTEXTRA", save_root);
    CHECK(!exists(extra));
    for (int i = 0; i < SLOTS; ++i) CHECK(api.umount(&r[i].point) == 0);
    CHECK(mount_dir("SLOTEXTRA", MODE_CREATE | MOUNT_RDWR, &r[SLOTS]) == 0);
    CHECK(!strcmp(r[SLOTS].point.data, "/savedata0"));
    CHECK(api.umount(&r[SLOTS].point) == 0);
}

/* A write interrupted before its rename leaves the previous save whole; the rename then replaces it. */
static void test_interrupted_write(void) {
    MountResult r;
    CHECK(mount_dir("SAVEDATA00", MOUNT_RDWR, &r) == 0);
    char host[900]; snprintf(host, sizeof(host), "%s/SAVEDATA00/ROBO13.DAT", save_root);
    int64_t before = host_size(host);
    unsigned char half[30000]; memset(half, 0xab, sizeof(half));
    CHECK(write_file("/savedata0/ROBO13.TMP", half, sizeof(half), GUEST_WRONLY | GUEST_CREAT | GUEST_TRUNC) == (int64_t)sizeof(half));
    CHECK(host_size(host) == before);
    CHECK(api.rename("/savedata0/ROBO13.TMP", "/savedata0/ROBO13.DAT") == 0);
    CHECK(host_size(host) == (int64_t)sizeof(half));
    char temp[900]; snprintf(temp, sizeof(temp), "%s/SAVEDATA00/ROBO13.TMP", save_root);
    CHECK(!exists(temp));
    int64_t fd = api.open("/savedata0/ROBO13.DAT", GUEST_RDWR | GUEST_TRUNC, 0);
    CHECK(fd >= 3);
    CHECK(api.write((int)fd, half, 100) == 100);
    unsigned char stat_buffer[120]; int64_t size = -1;
    CHECK(api.fstat((int)fd, stat_buffer) == 0);
    memcpy(&size, stat_buffer + 72, 8);
    CHECK(size == 100);
    api.close((int)fd);
    CHECK(api.unlink("/savedata0/ROBO13.TMP") == KERNEL_ENOENT);
    CHECK(api.umount(&r.point) == 0);
}

static void test_corrupted_metadata(void) {
    char meta[900]; snprintf(meta, sizeof(meta), "%s/SAVEDATA00.sce_sys/param.bin", save_root);
    FILE *f = fopen(meta, "wb");
    if (f) { fwrite("garbage", 1, 7, f); fclose(f); }
    Param p;
    CHECK(runtime_savedata_param(1, "SAVEDATA00", &p) == -1);
    MountResult r;
    CHECK(mount_dir("SAVEDATA00", MODE_RDONLY, &r) == 0);
    CHECK(api.umount(&r.point) == 0);
    DirName names[8]; Param params[8]; SearchInfo infos[8];
    DirName pattern = dir_name("SAVE%");
    SearchCond cond; memset(&cond, 0, sizeof(cond)); cond.user = 1; cond.dir = &pattern;
    SearchResult result; memset(&result, 0, sizeof(result));
    result.names = names; result.names_capacity = 8; result.params = params; result.infos = infos;
    CHECK(api.search(&cond, &result) == 0);
    CHECK(result.hits == 1 && result.set_count == 1);
    CHECK(!strcmp(names[0].data, "SAVEDATA00"));
    CHECK(api.search(NULL, &result) == ERR_PARAMETER);
}

static void test_search_and_delete(void) {
    DirName names[2]; SearchCond cond; SearchResult result;
    memset(&cond, 0, sizeof(cond)); cond.user = 1;
    memset(&result, 0, sizeof(result)); result.names = names; result.names_capacity = 2;
    CHECK(api.search(&cond, &result) == 0);
    CHECK(result.hits == SLOTS + 2 && result.set_count == 2);
    CHECK(!strcmp(names[0].data, "SAVEDATA00") && !strcmp(names[1].data, "SLOT00"));
    cond.order = 1;
    CHECK(api.search(&cond, &result) == 0);
    CHECK(!strcmp(names[0].data, "SLOTEXTRA"));

    DirName dir = dir_name("SLOT05");
    Delete d; memset(&d, 0, sizeof(d)); d.user = 1; d.dir = &dir;
    CHECK(api.remove(&d) == 0);
    char path[900]; snprintf(path, sizeof(path), "%s/SLOT05", save_root);
    CHECK(!exists(path));
    snprintf(path, sizeof(path), "%s/SLOT05.sce_sys", save_root);
    CHECK(!exists(path));
    CHECK(api.remove(&d) == ERR_NOT_FOUND);
    DirName escape = dir_name("../..");
    d.dir = &escape;
    CHECK(api.remove(&d) == ERR_PARAMETER);
    CHECK(exists(user_root));
}

static void test_save_memory(void) {
    unsigned char out[64], in[64];
    CHECK(api.memory_get(1, in, sizeof(in), 0) == ERR_MEMORY_NOT_READY);
    CHECK(api.memory_setup(1, 4096, NULL) == 0);
    for (size_t i = 0; i < sizeof(out); ++i) out[i] = (unsigned char)(255 - i);
    CHECK(api.memory_set(1, out, sizeof(out), 1000) == 0);
    CHECK(api.memory_get(1, in, sizeof(in), 1000) == 0);
    CHECK(!memcmp(in, out, sizeof(in)));
    CHECK(api.memory_get(1, in, sizeof(in), 4090) == ERR_PARAMETER);
    CHECK(api.memory_get(1, in, sizeof(in), -1) == ERR_PARAMETER);
    CHECK(memory_writes == 1);
}

static void test_game_paths_read_only(void) {
    CHECK(api.open("/app0/eboot.bin", GUEST_WRONLY | GUEST_CREAT, 0644) == KERNEL_EROFS);
    CHECK(api.open("/savedata0/../escape", GUEST_RDONLY, 0) < 0);
}

/* The user's save, copied: mounted read-only, opened and read the way the game loads it. */
/* The prologue demo's save, offered by the Japanese and Asian games: found and read-only when present, else not found. */
static void test_transferring_mount(void) {
    typedef int32_t (ABI *TransferFn)(const TransferringMount *, MountResult *);
    const TransferFn transfer = (TransferFn)save_export("sceSaveDataTransferringMount");
    TitleId demo; memset(&demo, 0, sizeof(demo)); memcpy(demo.data, "CUSA17627", 9);
    DirName dir = dir_name("SAVEDATA00");
    TransferringMount m; memset(&m, 0, sizeof(m));
    m.user = 1; m.title = &demo; m.dir = &dir;
    MountResult r;
    CHECK(transfer(&m, &r) == ERR_NOT_FOUND);
    char demo_dir[900];
    snprintf(demo_dir, sizeof(demo_dir), "%s/savedata/1/CUSA17627/SAVEDATA00", user_root);
    CHECK(make_dirs(demo_dir) == 0);
    char demo_file[960];
    snprintf(demo_file, sizeof(demo_file), "%s/ROBO13.DAT", demo_dir);
    FILE *f = fopen(demo_file, "wb");
    if (f) { fputs("demo", f); fclose(f); }
    CHECK(transfer(&m, &r) == 0 && r.status == 0);
    char back[8] = {0};
    char guest[64]; snprintf(guest, sizeof(guest), "%s/ROBO13.DAT", r.point.data);
    const int64_t fd = api.open(guest, GUEST_RDONLY, 0);
    CHECK(fd >= 3 && api.read((int)fd, back, 4) == 4 && !memcmp(back, "demo", 4));
    if (fd >= 3) api.close((int)fd);
    CHECK(api.open(guest, GUEST_WRONLY | GUEST_TRUNC, 0) < 0);
    CHECK(api.umount(&r.point) == 0);
    CHECK(exists(demo_file) && host_size(demo_file) == 4);
    m.title = NULL;
    CHECK(transfer(&m, &r) == ERR_PARAMETER);
    TitleId escape; memset(&escape, 0, sizeof(escape)); memcpy(escape.data, "../x", 4);
    m.title = &escape;
    CHECK(transfer(&m, &r) == ERR_PARAMETER);
    CHECK(transfer(NULL, &r) == ERR_PARAMETER);
}

static void test_user_save_copy(const char *repo) {
    char original[1100], original_meta[1100];
    snprintf(original, sizeof(original), "%s/user/savedata/1/CUSA19610/SAVEDATA00/ROBO13.DAT", repo);
    snprintf(original_meta, sizeof(original_meta), "%s/user/savedata/1/CUSA19610/SAVEDATA00.sce_sys/param.bin", repo);
    struct stat before_stat;
    if (stat(original, &before_stat)) { printf("skipped: no user save at user/savedata/1/CUSA19610/SAVEDATA00\n"); return; }
    int64_t size = 0, meta_size = 0, after_size = 0;
    uint64_t hash = file_hash(original, &size), meta_hash = file_hash(original_meta, &meta_size);

    char dir[900], meta_dir[900], copy[1000], meta_copy[1000];
    snprintf(dir, sizeof(dir), "%s/USERCOPY", save_root);
    snprintf(meta_dir, sizeof(meta_dir), "%s/USERCOPY.sce_sys", save_root);
    mkdir(dir, 0755); mkdir(meta_dir, 0755);
    snprintf(copy, sizeof(copy), "%s/ROBO13.DAT", dir);
    snprintf(meta_copy, sizeof(meta_copy), "%s/param.bin", meta_dir);
    CHECK(copy_file(original, copy));
    if (meta_size) CHECK(copy_file(original_meta, meta_copy));

    MountResult r;
    CHECK(mount_dir("USERCOPY", MODE_RDONLY, &r) == 0);
    int64_t fd = api.open("/savedata0/ROBO13.DAT", GUEST_RDONLY, 0);
    CHECK(fd >= 3);
    unsigned char stat_buffer[120]; int64_t guest_size = -1;
    CHECK(api.fstat((int)fd, stat_buffer) == 0);
    memcpy(&guest_size, stat_buffer + 72, 8);
    CHECK(guest_size == size);
    unsigned char *data = malloc((size_t)size);
    int64_t total = 0;
    for (int64_t n; data && total < size && (n = api.read((int)fd, data + total, (uint64_t)(size - total < 65536 ? size - total : 65536))) > 0;) total += n;
    CHECK(total == size);
    CHECK(api.read((int)fd, stat_buffer, 16) == 0);
    if (data && size >= 16) {
        uint32_t stored; memcpy(&stored, data + 12, 4);
        CHECK(!memcmp(data + 4, "ROSD", 4));
        CHECK(stored == (uint32_t)size);
        uint64_t h = UINT64_C(1469598103934665603);
        for (int64_t i = 0; i < size; ++i) { h ^= data[i]; h *= UINT64_C(1099511628211); }
        CHECK(h == hash);
    }
    free(data);
    api.close((int)fd);
    Param p;
    CHECK(!meta_size || runtime_savedata_param(1, "USERCOPY", &p) == 0);
    CHECK(api.umount(&r.point) == 0);

    struct stat after_stat;
    CHECK(!stat(original, &after_stat) && after_stat.st_mtime == before_stat.st_mtime);
    CHECK(file_hash(original, &after_size) == hash && after_size == size);
    int64_t meta_after = 0;
    CHECK(!meta_size || (file_hash(original_meta, &meta_after) == meta_hash && meta_after == meta_size));
    printf("user save copy: %lld bytes read through a read-only mount, original unchanged\n", (long long)size);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    snprintf(test_root, sizeof(test_root), "%s", TSR_SAVEDATA_TEST_ROOT);
    snprintf(user_root, sizeof(user_root), "%s/user", test_root);
    snprintf(save_root, sizeof(save_root), "%s/savedata/1/CUSA19610", user_root);
    wipe(test_root);
    mkdir(test_root, 0755);
    char app0[760]; snprintf(app0, sizeof(app0), "%s/app0", test_root);
    mkdir(app0, 0755);
    runtime_file_configure(app0, user_root);
    runtime_savedata_configure("CUSA19610");
    resolve_api();

    test_requires_initialize();
    test_missing_and_invalid();
    test_create_write_and_reload();
    test_directory_listing();
    test_repeated_mounts();
    test_slots_full();
    test_interrupted_write();
    test_corrupted_metadata();
    test_search_and_delete();
    test_save_memory();
    test_game_paths_read_only();
    test_transferring_mount();
    test_user_save_copy(TSR_SOURCE_DIR);

    wipe(test_root);
    printf("savedata: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
