#include "skip_intro.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "code_signature.h"

/* The logo scene's wait: the layout-finished test, then the button poll; either advances to the fade-out state. */
static const short kScene[] = {
    0x48, 0x8d, 0x05, CODE_ANY, CODE_ANY, CODE_ANY, CODE_ANY,       /* lea rax, [rip+...] */
    0x8b, 0x40, 0x34,                                               /* mov eax, [rax+0x34] */
    0x41, 0x85, 0x87, CODE_ANY, CODE_ANY, 0x00, 0x00,               /* test [r15+...], eax */
    0x0f, 0x85, 0xda, 0x00, 0x00, 0x00,                             /* jne advance */
    0x48, 0x89, 0xdf,                                               /* mov rdi, rbx */
    0xe8, CODE_ANY, CODE_ANY, CODE_ANY, CODE_ANY,                   /* call any_player_pressed */
    0x84, 0xc0,                                                     /* test al, al */
    0x0f, 0x85, 0xca, 0x00, 0x00, 0x00,                             /* jne advance */
    0xe9, 0xd6, 0x00, 0x00, 0x00,                                   /* jmp stay */
};
enum { kCallInScene = 26, kCallLength = 5 };

/* any_player_pressed: walks the player pads through the scene's input mask. */
static const short kPoll[] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x4c, 0x8d, 0x35, CODE_ANY, CODE_ANY, CODE_ANY, CODE_ANY,
    0x8b, 0x87, CODE_ANY, 0x00, 0x00, 0x00, 0x48, 0x89, 0xfb, 0x41, 0x8b, 0x4e, 0x04, 0x85, 0xc1, 0x74,
};

typedef struct {
    const char *build;
    uint32_t scene;
    uint32_t poll;
} BuildSite;

static const BuildSite kBuilds[] = {
    {"CUSA19610-1.00", 0x3aff4d, 0x36f400},
    {"CUSA19610-1.01", 0x3b143d, 0x36fd90},
    {"CUSA19610-1.02", 0x36a92d, 0x32a410},
};

static atomic_int armed;

void skip_intro_arm(void) { atomic_store(&armed, 1); }

int skip_intro_claim(void) { return atomic_exchange(&armed, 0); }

/* At the poll call: a claimed press returns true without polling. */
void GUEST_HOOK_ABI skip_intro_hook(GuestRegs *regs) {
    if (!skip_intro_claim()) return;
    regs->rax = 1;
    regs->skip = 1;
}

SkipIntroLocate skip_intro_locate(const uint8_t *image, uint64_t size, const char *build, uint32_t *site) {
    const BuildSite *known = NULL;
    for (size_t i = 0; build && i < sizeof(kBuilds) / sizeof(*kBuilds); ++i) {
        if (!strcmp(build, kBuilds[i].build)) known = &kBuilds[i];
    }
    if (!known) return SKIP_INTRO_UNKNOWN_BUILD;
    uint64_t first = 0;
    if (code_signature_count(image, size, kScene, sizeof(kScene) / sizeof(*kScene), &first) != 1 || first != known->scene)
        return SKIP_INTRO_MISMATCH;
    const uint32_t call = known->scene + kCallInScene;
    int32_t displacement;
    memcpy(&displacement, image + call + 1, 4);
    if ((int64_t)call + kCallLength + displacement != (int64_t)known->poll) return SKIP_INTRO_MISMATCH;
    if ((uint64_t)known->poll + sizeof(kPoll) / sizeof(*kPoll) > size ||
        !code_signature_matches(image + known->poll, kPoll, sizeof(kPoll) / sizeof(*kPoll)))
        return SKIP_INTRO_MISMATCH;
    if (site) *site = call;
    return SKIP_INTRO_FOUND;
}

size_t skip_intro_stub_bytes(void) { return guest_hook_stub_size(); }

int skip_intro_install(uint8_t *image, uint64_t size, uint64_t entry, const char *build, uint8_t *stubs,
                       size_t stubs_size) {
    uint32_t site = 0;
    switch (skip_intro_locate(image, size, build, &site)) {
    case SKIP_INTRO_FOUND: break;
    case SKIP_INTRO_UNKNOWN_BUILD:
        printf("Skip Intro: not installed: not a verified game build\n");
        return 0;
    case SKIP_INTRO_MISMATCH:
        printf("Skip Intro: not installed: the logo scene code of %s is not the verified one\n", build);
        return 0;
    }
    GuestHook hook = {"skip intro", site, kCallLength, {0}, skip_intro_hook};
    memcpy(hook.original, image + site, kCallLength);
    const GuestImageId id = {size, entry};
    size_t bad = 0;
    const GuestHookStatus status = guest_hooks_install(image, size, entry, &id, &hook, 1, stubs, stubs_size, &bad);
    if (status != GUEST_HOOK_OK) {
        printf("Skip Intro: not installed: %s\n", guest_hook_status_text(status));
        return 0;
    }
    skip_intro_arm();
    printf("Skip Intro: hook installed at 0x%x\n", (unsigned)site);
    return 1;
}
