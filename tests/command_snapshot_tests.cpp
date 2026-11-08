// Graphics submissions: the command processor reads the words the game submitted, even when the game writes that
// memory again before the GPU thread has read it (JPN 1.00 rebuilt its next frame in place: a float of the new
// frame was read as a packet header).
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "video_core/amdgpu/command_snapshot.h"

namespace {

int checks = 0;
int failures = 0;
#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(condition)) {                                                                                            \
            ++failures;                                                                                                \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);                                                   \
        }                                                                                                              \
    } while (0)

constexpr u32 Nop = 0x10, SetContextReg = 0x69, Rewind = 0x59, DrawIndexAuto = 0x2d;
constexpr u32 One = 0x3f800000;

u32 Type3(u32 opcode, u32 words) {
    return 0xc0000000u | ((words - 2) << 16) | (opcode << 8);
}

struct Walk {
    std::vector<size_t> headers;
    bool bad = false;
    size_t bad_at = 0;
};

Walk WalkPackets(std::span<const u32> stream) {
    Walk walk;
    for (size_t at = 0; at < stream.size();) {
        const u32 header = stream[at];
        if (header >> 30 == 2) {
            ++at;
            continue;
        }
        if (header >> 30 != 3) {
            walk.bad = true;
            walk.bad_at = at;
            break;
        }
        walk.headers.push_back(at);
        at += ((header >> 16) & 0x3fff) + 2;
    }
    return walk;
}

// One frame: register packets, vertex floats embedded in a NOP, a draw; the NOP size changes from frame to frame.
std::vector<u32> Frame(u32 embedded, u32 total) {
    std::vector<u32> words;
    for (u32 i = 0; i < 8; ++i) {
        words.push_back(Type3(SetContextReg, 3 + i % 4));
        for (u32 j = 0; j < 2 + i % 4; ++j) {
            words.push_back(0x200 + i * 8 + j);
        }
    }
    words.push_back(Type3(Nop, embedded + 2));
    words.push_back(0);
    for (u32 i = 0; i < embedded; ++i) {
        words.push_back(i % 5 == 3 ? One : 0);
    }
    while (words.size() + 3 <= total) {
        words.push_back(Type3(DrawIndexAuto, 3));
        words.push_back(6);
        words.push_back(2);
    }
    while (words.size() < total) {
        words.push_back(0x80000000u);
    }
    return words;
}

constexpr u32 Total = 1500;

void InPlaceReadingSeesTheNextFrame() {
    const auto submitted = Frame(657, Total), next = Frame(694, Total);
    std::vector<u32> guest = submitted;
    const Walk expected = WalkPackets(submitted);
    CHECK(!expected.bad);
    CHECK(!WalkPackets(next).bad);

    // A parser reading the game's memory: the next frame is written after it has read the NOP header.
    size_t at = 0;
    bool rewritten = false, bad = false;
    while (at < guest.size()) {
        const u32 header = guest[at];
        if (header >> 30 != 3) {
            bad = true;
            break;
        }
        at += ((header >> 16) & 0x3fff) + 2;
        if (((header >> 8) & 0xff) == Nop && !rewritten) {
            guest = next;
            rewritten = true;
        }
    }
    CHECK(rewritten);
    CHECK(bad);
    CHECK(at < guest.size() && (guest[at] == 0 || guest[at] == One));
    const Walk fresh = WalkPackets(guest);
    bool fresh_lands = false;
    for (size_t header : fresh.headers) {
        fresh_lands |= header == at;
    }
    CHECK(!fresh_lands);
}

void SnapshotKeepsTheSubmittedStream() {
    const auto submitted = Frame(657, Total), next = Frame(694, Total);
    std::vector<u32> guest = submitted;
    const Walk expected = WalkPackets(submitted);

    AmdGpu::CommandSnapshots snapshots;
    auto snapshot = snapshots.Take(guest, {});
    CHECK(snapshot->guest_dcb == guest.data());
    CHECK(snapshot->dcb.data() != guest.data());
    CHECK(snapshot->ccb.empty());

    guest = next;
    const Walk walk = WalkPackets(snapshot->dcb);
    CHECK(!walk.bad);
    CHECK(walk.headers == expected.headers);
    CHECK(AmdGpu::FirstRewrittenWord(snapshot->dcb, guest.data()) < snapshot->dcb.size());

    guest = submitted;
    CHECK(AmdGpu::FirstRewrittenWord(snapshot->dcb, guest.data()) == snapshot->dcb.size());
    snapshots.Return(std::move(snapshot));
}

void SnapshotHoldsWhileTheGameWrites() {
    const auto submitted = Frame(657, Total), next = Frame(694, Total);
    std::vector<u32> guest = submitted;
    const Walk expected = WalkPackets(submitted);

    AmdGpu::CommandSnapshots snapshots;
    auto snapshot = snapshots.Take(guest, {});
    std::atomic<bool> stop{false};
    std::atomic<u32> writes{0};
    std::thread game([&] {
        auto* words = reinterpret_cast<volatile u32*>(guest.data());
        for (u32 frame = 0; !stop.load(std::memory_order_relaxed); ++frame) {
            const auto& source = frame % 2 ? submitted : next;
            for (size_t i = 0; i < source.size(); ++i) {
                words[i] = source[i];
            }
            writes.fetch_add(1, std::memory_order_relaxed);
        }
    });
    while (writes.load() == 0) {
        std::this_thread::yield();
    }
    bool stable = true;
    for (int pass = 0; pass < 2000 || writes.load() < 4; ++pass) {
        const Walk walk = WalkPackets(snapshot->dcb);
        stable &= !walk.bad && walk.headers == expected.headers;
    }
    stop = true;
    game.join();
    CHECK(stable);
    CHECK(writes.load() >= 4);
    snapshots.Return(std::move(snapshot));
}

void ConstantCommandsFollowTheDrawCommands() {
    std::vector<u32> dcb = Frame(20, 200);
    std::vector<u32> ccb = {Type3(0x84, 4), 1, 2, 3};
    AmdGpu::CommandSnapshots snapshots;
    auto snapshot = snapshots.Take(dcb, ccb);
    CHECK(snapshot->dcb.size() == dcb.size());
    CHECK(snapshot->ccb.size() == ccb.size());
    CHECK(snapshot->ccb.data() == snapshot->dcb.data() + dcb.size());
    CHECK(std::memcmp(snapshot->ccb.data(), ccb.data(), ccb.size() * 4) == 0);
    std::fill(ccb.begin(), ccb.end(), 0u);
    CHECK(snapshot->ccb[0] == Type3(0x84, 4));
}

void StorageIsReused() {
    AmdGpu::CommandSnapshots snapshots;
    const auto big = Frame(694, 70235), small = Frame(20, 300);
    auto first = snapshots.Take(big, {});
    const u32* storage = first->words.data();
    snapshots.Return(std::move(first));
    CHECK(snapshots.SpareCount() == 1);
    auto second = snapshots.Take(small, {});
    CHECK(second->words.data() == storage);
    CHECK(second->dcb.size() == small.size());
    CHECK(WalkPackets(second->dcb).headers == WalkPackets(small).headers);
    snapshots.Return(std::move(second));
    auto third = snapshots.Take(big, {});
    CHECK(third->words.data() == storage);
    snapshots.Return(std::move(third));

    std::vector<std::unique_ptr<AmdGpu::CommandSnapshot>> in_flight;
    for (int i = 0; i < 12; ++i) {
        in_flight.push_back(snapshots.Take(small, {}));
    }
    for (auto& snapshot : in_flight) {
        snapshots.Return(std::move(snapshot));
    }
    CHECK(snapshots.SpareCount() == AmdGpu::CommandSnapshots::MaxSpare);
    snapshots.Return(nullptr);
    CHECK(snapshots.SpareCount() == AmdGpu::CommandSnapshots::MaxSpare);
}

void PacketsAfterARewindStayTheGames() {
    std::vector<u32> guest = Frame(20, 120);
    const size_t rewind_at = guest.size();
    guest.push_back(Type3(Rewind, 2));
    guest.push_back(0);
    guest.resize(guest.size() + 30, 0x80000000u);

    AmdGpu::CommandSnapshots snapshots;
    auto snapshot = snapshots.Take(guest, {});
    guest[rewind_at + 1] = 1u << 31;
    guest[rewind_at + 2] = Type3(DrawIndexAuto, 3);
    const std::span<const u32> rest = snapshot->dcb.subspan(rewind_at);
    const auto live = AmdGpu::GuestRest(rest, snapshot->dcb.data(), snapshot->guest_dcb);
    CHECK(live.data() == guest.data() + rewind_at);
    CHECK(live.size() == rest.size());
    CHECK(rest[1] == 0);
    CHECK(live[1] >> 31 == 1);
    CHECK(live[2] == Type3(DrawIndexAuto, 3));
}

std::string ReadText(const char* relative) {
    std::ifstream in(std::string(TSR_SOURCE_DIR) + "/" + relative, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

void LiverpoolParsesTheSnapshot() {
    const std::string liverpool = ReadText("gpu/shadps4/video_core/amdgpu/liverpool.cpp");
    CHECK(!liverpool.empty());
    CHECK(liverpool.find("auto commands = command_snapshots.Take(dcb, ccb);") != std::string::npos);
    CHECK(liverpool.find("ProcessGraphics(commands->dcb, commands->ccb, seq, commands->guest_dcb)") !=
          std::string::npos);
    CHECK(liverpool.find("command_snapshots.Return(std::move(queue.submits.front().commands));") !=
          std::string::npos);
    CHECK(liverpool.find("dcb = GuestRest(dcb, reinterpret_cast<const u32*>(base_addr), guest_dcb);") !=
          std::string::npos);
    CHECK(liverpool.find("IsCopyGpuBuffers") == std::string::npos);
    CHECK(liverpool.find("DEBUG_" "SAULO") == std::string::npos);
}

} // namespace

int main() {
    InPlaceReadingSeesTheNextFrame();
    SnapshotKeepsTheSubmittedStream();
    SnapshotHoldsWhileTheGameWrites();
    ConstantCommandsFollowTheDrawCommands();
    StorageIsReused();
    PacketsAfterARewindStayTheGames();
    LiverpoolParsesTheSnapshot();
    std::printf("command snapshot tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
