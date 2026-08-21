// mge_wearable_gates — the wearables system's acceptance gates for the body
// (task 13.10, BODY_CONTRACT.md §9).
//
// Run it after any change to the body and read the seven lines. Per P12 the
// point is that nobody needs an engineer to interpret the answer: each gate
// says PASS, FAIL or BLOCKED and, in the same breath, why.
//
// Exit code is 0 when nothing FAILED. BLOCKED gates do not fail the run —
// they are waiting on a body deliverable that has not shipped, and they name
// the task that would unblock them.

#include <cstdio>
#include <string>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/character/garment_binding.h"
#include "mge/import/wearable_gates.h"

using namespace mge;

int main(int argc, char** argv) {
    if (argc > 1) setCharacterAssetDir(argv[1]);

    const std::vector<SkinnedMeshData>& lods = sharedTemplateLods();
    if (lods.empty() || lods[0].vertices.empty()) {
        std::printf("no template body found in %s\n", characterAssetDir());
        return 1;
    }

    std::printf("body: %zu vertices, %zu triangles, hash %016llx\n",
                lods[0].vertices.size(), lods[0].triangleCount(),
                static_cast<unsigned long long>(skinnedMeshContentHash(lods[0])));
    std::printf("wearables acceptance gates (BODY_CONTRACT.md §9)\n\n");

    GateReport report;
    runWearableGates(lods, report);

    for (const GateResult& gate : report.gates) {
        std::printf("  %-7s %-9s %s\n", gateStatusName(gate.status), gate.id, gate.name);
        // Wrap the reason so a long explanation stays readable in a terminal.
        const std::string detail = gate.detail;
        size_t start = 0;
        while (start < detail.size()) {
            size_t end = start + 68;
            if (end < detail.size()) {
                const size_t space = detail.rfind(' ', end);
                if (space != std::string::npos && space > start) end = space;
            } else {
                end = detail.size();
            }
            std::printf("            %s\n", detail.substr(start, end - start).c_str());
            start = end < detail.size() && detail[end] == ' ' ? end + 1 : end;
        }
        std::printf("\n");
    }

    const size_t failed = report.countOf(GateStatus::Fail);
    const size_t blocked = report.countOf(GateStatus::Blocked);
    std::printf("%zu passed, %zu failed, %zu blocked\n",
                report.countOf(GateStatus::Pass), failed, blocked);
    return failed == 0 ? 0 : 1;
}
