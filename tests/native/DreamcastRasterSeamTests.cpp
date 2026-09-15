#include "platform/dreamcast/DreamcastRasterRenderer.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression) {
    if (!condition) {
        std::cerr << "expected " << expression << '\n';
        ++g_failures;
    }
}

#define EXPECT(condition) expect((condition), #condition)

void PacketConversionInitializesEveryField() {
    using namespace helengine::dreamcast;
    const ProjectedVertex input{123.5f, 234.25f, 0.25f, {1.0f, 0.5f, 0.0f, 1.0f}};
    const renderer_detail::VertexPacket packet =
        renderer_detail::makeVertexPacket(input, false);

    EXPECT(packet.flags == renderer_detail::kVertexCommand);
    EXPECT(packet.x == input.x);
    EXPECT(packet.y == input.y);
    EXPECT(packet.z == input.z);
    EXPECT(packet.u == 0.0f);
    EXPECT(packet.v == 0.0f);
    EXPECT(packet.argb == 0xffff8000u);
    EXPECT(packet.oargb == 0u);
}

void PacketConversionMarksOnlyThirdVertexEndOfLine() {
    using namespace helengine::dreamcast;
    const ProjectedVertex input{0.0f, 0.0f, 1.0f, {0.25f, 0.25f, 0.25f, 1.0f}};
    const renderer_detail::VertexPacket packet =
        renderer_detail::makeVertexPacket(input, true);

    EXPECT(packet.flags == renderer_detail::kVertexCommandEol);
}

void SimulationDeltaIsClampedToAvoidDebuggerJumps() {
    using namespace helengine::dreamcast;
    EXPECT(renderer_detail::clampSimulationDelta(-1.0f) == 0.0f);
    EXPECT(renderer_detail::clampSimulationDelta(0.05f) == 0.05f);
    EXPECT(renderer_detail::clampSimulationDelta(0.5f) == 0.10f);
    EXPECT(renderer_detail::clampSimulationDelta(NAN) == 0.0f);
}

} // namespace

int main() {
    PacketConversionInitializesEveryField();
    PacketConversionMarksOnlyThirdVertexEndOfLine();
    SimulationDeltaIsClampedToAvoidDebuggerJumps();

    if (g_failures != 0) {
        std::cerr << g_failures << " assertion(s) failed\n";
        return 1;
    }
    std::cout << "DreamcastRasterSeamTests: all tests passed\n";
    return 0;
}
