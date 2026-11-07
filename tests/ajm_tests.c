/* Ajm (ATRAC9) contexts, instances and batches through their exports, with batches built by the same builder exports
 * the game uses: slot allocation and release, pending batches, exhaustion, malformed batches, error codes. */
#include "runtime_ajm.c"

static int checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    for (size_t i = 0; i < count; ++i) if (!strcmp(table[i].name, name)) return (uintptr_t)table[i].function;
    return 0;
}

static size_t pending_batches(void) {
    size_t n = 0;
    for (int i = 0; i < MAX_BATCHES; ++i) n += batches[i].used != 0;
    return n;
}
static uint32_t reset_batch(unsigned char *buffer, uint32_t instance, SidebandResult *result) {
    unsigned char *end = job_control(buffer, instance, UINT64_C(1) << 13, NULL, 0, result, sizeof(*result), NULL);
    return (uint32_t)(end - buffer);
}

static void test_context_and_instances(uint32_t *ctx, uint32_t *instance) {
    uint32_t id = 0;
    CHECK(ajm_initialize(1, &id) == ERR_INVALID_PARAMETER);
    CHECK(ajm_initialize(0, NULL) == ERR_INVALID_PARAMETER);
    CHECK(ajm_initialize(0, &id) == 0 && id >= 1);
    CHECK(ajm_instance_create(id, CODEC_AT9, 1, instance) == ERR_NOT_REGISTERED);
    CHECK(ajm_module_register(id, CODEC_AT9, 0) == 0);
    CHECK(ajm_module_register(id, CODEC_AT9, 0) == ERR_ALREADY_REGISTERED);
    CHECK(ajm_module_register(id, 24, 0) == ERR_INVALID_PARAMETER);
    CHECK(ajm_instance_create(id, CODEC_AT9, 0, instance) == ERR_WRONG_REVISION);
    CHECK(ajm_instance_create(id, CODEC_AT9, 1 | (7 << 7), instance) == ERR_INVALID_PARAMETER);
    CHECK(ajm_instance_create(id, CODEC_AT9, 1 | (FORMAT_FLOAT << 7) | (2 << 3), instance) == 0);
    CHECK(ajm_instance_codec(*instance) == CODEC_AT9);
    CHECK(ajm_instance_create(id + 1, CODEC_AT9, 1, instance + 1) == ERR_INVALID_CONTEXT);
    *ctx = id;
}

static void test_batch_lifecycle(uint32_t ctx, uint32_t instance) {
    unsigned char buffer[256];
    SidebandResult result = {-1, -1};
    uint32_t size = reset_batch(buffer, instance, &result);
    BatchError error;
    uint32_t batch = 0;
    size_t jobs_before = jobs_run, pending_before = pending_batches();
    CHECK(ajm_batch_start(ctx, buffer, size, 0, &error, &batch) == 0);
    CHECK(batch >= 1 && batch <= MAX_BATCHES);
    CHECK(jobs_run == jobs_before + 1);
    CHECK(result.result == 0);
    CHECK(pending_batches() == pending_before + 1);
    CHECK(ajm_batch_cancel(ctx, batch) == 0);
    CHECK(ajm_batch_wait(ctx, batch, 0, &error) == 0);
    CHECK(pending_batches() == pending_before);
    CHECK(ajm_batch_wait(ctx, batch, 0, &error) == ERR_INVALID_BATCH);
    CHECK(ajm_batch_cancel(ctx, batch) == ERR_INVALID_BATCH);
    CHECK(ajm_batch_wait(ctx, 0, 0, &error) == ERR_INVALID_BATCH);
    CHECK(ajm_batch_wait(ctx, MAX_BATCHES + 1, 0, &error) == ERR_INVALID_BATCH);
    CHECK(ajm_batch_wait(ctx + 1, 1, 0, &error) == ERR_INVALID_CONTEXT);

    const uint64_t note = 0x1122334455667788;
    unsigned char *end = job_inline(buffer, &note, sizeof(note), NULL);
    end = job_control(end, instance, UINT64_C(1) << 13, NULL, 0, &result, sizeof(result), NULL);
    CHECK(ajm_batch_start(ctx, buffer, (uint32_t)(end - buffer), 0, &error, &batch) == 0);
    CHECK(ajm_batch_wait(ctx, batch, 0, &error) == 0);
}

/* Slots are only freed by a wait: a game that never waits runs out, and gets them back once it does. */
static void test_slot_exhaustion(uint32_t ctx, uint32_t instance) {
    unsigned char buffer[256];
    SidebandResult result;
    uint32_t size = reset_batch(buffer, instance, &result);
    BatchError error;
    uint32_t ids[MAX_BATCHES];
    int started = 1;
    for (int i = 0; i < MAX_BATCHES; ++i) if (ajm_batch_start(ctx, buffer, size, 0, &error, &ids[i])) started = 0;
    CHECK(started);
    CHECK(pending_batches() == MAX_BATCHES);
    uint32_t extra = 0;
    CHECK(ajm_batch_start(ctx, buffer, size, 0, &error, &extra) == ERR_OUT_OF_RESOURCES);
    int waited = 1;
    for (int i = 0; i < MAX_BATCHES; ++i) if (ajm_batch_wait(ctx, ids[i], 0, &error)) waited = 0;
    CHECK(waited);
    CHECK(pending_batches() == 0);
    CHECK(ajm_batch_start(ctx, buffer, size, 0, &error, &extra) == 0);
    CHECK(ajm_batch_wait(ctx, extra, 0, &error) == 0);
}

static void test_malformed_batches(uint32_t ctx, uint32_t instance) {
    unsigned char buffer[256];
    SidebandResult result;
    uint32_t size = reset_batch(buffer, instance, &result);
    BatchError error;
    uint32_t batch = 0;
    size_t pending_before = pending_batches();
    CHECK(ajm_batch_start(ctx, buffer, size - 4, 0, &error, &batch) == ERR_MALFORMED_BATCH);
    Chunk bogus = {header(IDENT_JOB, instance), 8};
    uint64_t junk = UINT64_C(0x3f);
    memcpy(buffer, &bogus, sizeof(bogus));
    memcpy(buffer + sizeof(bogus), &junk, sizeof(junk));
    memset(&error, 0, sizeof(error));
    CHECK(ajm_batch_start(ctx, buffer, 16, 0, &error, &batch) == ERR_MALFORMED_BATCH);
    CHECK(error.error_code == ERR_MALFORMED_BATCH && error.command_offset == 0);
    Chunk overrun = {header(IDENT_JOB, instance), 4096};
    memcpy(buffer, &overrun, sizeof(overrun));
    CHECK(ajm_batch_start(ctx, buffer, 16, 0, &error, &batch) == ERR_MALFORMED_BATCH);
    CHECK(pending_batches() == pending_before);
    CHECK(ajm_batch_start(ctx + 1, buffer, 16, 0, &error, &batch) == ERR_INVALID_CONTEXT);
}

static void test_destroyed_instance(uint32_t ctx, uint32_t instance) {
    CHECK(ajm_instance_destroy(ctx, instance) == 0);
    CHECK(ajm_instance_destroy(ctx, instance) == ERR_INVALID_INSTANCE);
    unsigned char buffer[256];
    SidebandResult result = {0, 0};
    uint32_t size = reset_batch(buffer, instance, &result);
    BatchError error;
    uint32_t batch = 0;
    CHECK(ajm_batch_start(ctx, buffer, size, 0, &error, &batch) == 0);
    CHECK((uint32_t)result.result == (RESULT_FATAL | RESULT_INVALID_PARAMETER));
    CHECK(ajm_batch_wait(ctx, batch, 0, &error) == 0);
}

static void test_finalize(uint32_t ctx) {
    uint32_t instance = 0;
    CHECK(ajm_instance_create(ctx, CODEC_AT9, 1, &instance) == 0);
    CHECK(ajm_module_unregister(ctx, CODEC_AT9) == 0);
    CHECK(ajm_module_unregister(ctx, CODEC_AT9) == ERR_NOT_REGISTERED);
    CHECK(ajm_finalize(ctx) == 0);
    CHECK(ajm_finalize(ctx) == ERR_INVALID_CONTEXT);
    uint32_t again = 0;
    CHECK(ajm_initialize(0, &again) == 0 && again == ctx);
    CHECK(ajm_finalize(again) == 0);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    CHECK(runtime_ajm_resolve("sceAjmBatchStartBuffer") == (uintptr_t)ajm_batch_start);
    CHECK(runtime_ajm_resolve("sceAjmBatchWait") == (uintptr_t)ajm_batch_wait);
    uint32_t ctx = 0, instance[2] = {0, 0};
    test_context_and_instances(&ctx, instance);
    test_batch_lifecycle(ctx, instance[0]);
    test_slot_exhaustion(ctx, instance[0]);
    test_malformed_batches(ctx, instance[0]);
    test_destroyed_instance(ctx, instance[0]);
    test_finalize(ctx);
    runtime_ajm_report();
    printf("not covered: ATRAC9 frame decoding (no AT9 sample in the repository)\n");
    printf("ajm: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
