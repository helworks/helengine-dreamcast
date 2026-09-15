#include "platform/dreamcast/DreamcastRasterRenderer.hpp"

#include "platform/dreamcast/DreamcastCubeMesh.hpp"
#include "platform/dreamcast/DreamcastBootHost.hpp"

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

void ThreePacketSequenceHasExactlyOneEndOfLine() {
    using namespace helengine::dreamcast;
    const ProjectedVertex input{0.0f, 0.0f, 1.0f, {1.0f, 1.0f, 1.0f, 1.0f}};
    const renderer_detail::VertexPacket packets[] = {
        renderer_detail::makeVertexPacket(input, false),
        renderer_detail::makeVertexPacket(input, false),
        renderer_detail::makeVertexPacket(input, true),
    };

    EXPECT(packets[0].flags == renderer_detail::kVertexCommand);
    EXPECT(packets[1].flags == renderer_detail::kVertexCommand);
    EXPECT(packets[2].flags == renderer_detail::kVertexCommandEol);
    EXPECT((packets[0].flags == renderer_detail::kVertexCommandEol) +
               (packets[1].flags == renderer_detail::kVertexCommandEol) +
               (packets[2].flags == renderer_detail::kVertexCommandEol) ==
           1);
}

float projectedArea(const helengine::dreamcast::ProjectedTriangle& triangle) {
    const helengine::dreamcast::ProjectedVertex& a = triangle.vertices[0];
    const helengine::dreamcast::ProjectedVertex& b = triangle.vertices[1];
    const helengine::dreamcast::ProjectedVertex& c = triangle.vertices[2];
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

void FixedPoseFrontAndBackSignsMatchDefaultCullContract() {
    using namespace helengine::dreamcast;
    const Matrix4x4 fixedPose{{
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 6.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    }};
    const GeometryConfig config{640.0f, 480.0f, 1.10f, 0.5f};
    GeometryFrame frame{};
    EXPECT(processGeometry(cubeMesh(), fixedPose, config, frame) == GeometryStatus::Ok);
    EXPECT(renderer_detail::kDefaultCullWinding ==
           renderer_detail::CullWinding::CounterClockwise);

    float redArea = 0.0f;
    float greenArea = 0.0f;
    for (std::uint16_t i = 0; i < frame.triangleCount; ++i) {
        const ProjectedTriangle& triangle = frame.triangles[i];
        const ColorRgba& color = triangle.vertices[0].color;
        if (color.r == 1.0f && color.g == 0.0f && color.b == 0.0f) {
            redArea = projectedArea(triangle);
        }
        if (color.r == 0.0f && color.g == 1.0f && color.b == 0.0f) {
            greenArea = projectedArea(triangle);
        }
    }
    // The near -Z green face is the visible front at this fixed pose. The
    // farther +Z red face has the opposite projected sign.
    EXPECT(greenArea > 0.0f);
    EXPECT(redArea < 0.0f);
}

void SimulationDeltaIsClampedToAvoidDebuggerJumps() {
    using namespace helengine::dreamcast;
    EXPECT(renderer_detail::clampSimulationDelta(-1.0f) == 0.0f);
    EXPECT(renderer_detail::clampSimulationDelta(0.05f) == 0.05f);
    EXPECT(renderer_detail::clampSimulationDelta(0.5f) == 0.10f);
    EXPECT(renderer_detail::clampSimulationDelta(NAN) == 0.0f);
}

void DiagnosticSnapshotLayoutIsStableForEmulatorReads() {
    using namespace helengine::dreamcast;
    EXPECT(sizeof(DreamcastDiagnosticSnapshot) == 80);
    EXPECT(offsetof(DreamcastDiagnosticSnapshot, frameIntervalNanosecondsLow) == 32);
    EXPECT(offsetof(DreamcastDiagnosticSnapshot, geometryNanosecondsLow) == 40);
    EXPECT(offsetof(DreamcastDiagnosticSnapshot, submissionNanosecondsLow) == 48);
    EXPECT(offsetof(DreamcastDiagnosticSnapshot, availableVramBytes) == 64);
    EXPECT(renderer_detail::kDiagnosticMagic == 0x48454443u);
    EXPECT(renderer_detail::kDiagnosticVersion == 1u);
}

void ScreenTriangleDiagnosticUsesFrontFacingWinding() {
    using namespace helengine::dreamcast;
    GeometryFrame frame{};
    boot_detail::makeScreenTriangleGeometry(640.0f, 480.0f, frame);

    EXPECT(frame.triangleCount == 1);
    EXPECT(frame.emittedVertexCount == 3);
    EXPECT(projectedArea(frame.triangles[0]) > 0.0f);
    EXPECT(frame.triangles[0].vertices[0].color.r == 1.0f);
    EXPECT(frame.triangles[0].vertices[1].color.b == 1.0f);
    EXPECT(frame.triangles[0].vertices[2].color.g == 1.0f);
}

} // namespace

int main() {
    PacketConversionInitializesEveryField();
    PacketConversionMarksOnlyThirdVertexEndOfLine();
    ThreePacketSequenceHasExactlyOneEndOfLine();
    FixedPoseFrontAndBackSignsMatchDefaultCullContract();
    SimulationDeltaIsClampedToAvoidDebuggerJumps();
    DiagnosticSnapshotLayoutIsStableForEmulatorReads();
    ScreenTriangleDiagnosticUsesFrontFacingWinding();

    if (g_failures != 0) {
        std::cerr << g_failures << " assertion(s) failed\n";
        return 1;
    }
    std::cout << "DreamcastRasterSeamTests: all tests passed\n";
    return 0;
}
