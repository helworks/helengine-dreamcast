#include "platform/dreamcast/DreamcastRasterRenderer.hpp"
#include "platform/dreamcast/DreamcastOverlayValidation.hpp"

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

void FixedPoseAuthoredWindingConversionIsExplicit() {
    using namespace helengine::dreamcast;
    const Matrix4x4 fixedPose{{
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 6.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    }};
    const GeometryConfig config{640.0f, 480.0f, 1.10f, 0.5f, 100.0f};
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
    // The authored +Z red face is nearest to a camera at +Z. The generated
    // camera -Z to native positive-depth reflection reverses its screen sign;
    // authored submission therefore selects clockwise front faces. Diagnostic
    // geometry retains the positive screen-space counterclockwise contract.
    EXPECT(redArea < 0.0f);
    EXPECT(greenArea > 0.0f);
    EXPECT(RendererConfig{}.frontFaceWinding == renderer_detail::CullWinding::CounterClockwise);
    RendererConfig authoredConfig{};
    authoredConfig.frontFaceWinding = renderer_detail::CullWinding::Clockwise;
    EXPECT(authoredConfig.frontFaceWinding == renderer_detail::CullWinding::Clockwise);
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

void MixedOverlayValidationAcceptsAndPreservesThreeKinds() {
    using namespace helengine::dreamcast;
    SpriteTexture texture{reinterpret_cast<void*>(1), 8, 4, 4};
    AuthoredOverlayFrame overlay{};
    overlay.sprites.spriteCount = 1;
    overlay.sprites.sprites[0] = {0, 0, 8, 8, 0, 0, 0.5f, 0.5f, &texture, {1, 1, 1, 1}};
    overlay.roundedRects[0] = {1, 2, 30, 20, 5, 2, kRoundedRectAllCorners,
                               {1, 0, 0, 0.8f}, {0, 0, 0, 1}};
    overlay.roundedRectCount = 1;
    overlay.glyphs[0] = {2, 3, 8, 10, 0, 0, 0.5f, 0.5f, &texture, {1, 1, 1, 1}};
    overlay.glyphCount = 1;
    overlay.order[0] = {AuthoredOverlayKind::Sprite, 0};
    overlay.order[1] = {AuthoredOverlayKind::RoundedRect, 0};
    overlay.order[2] = {AuthoredOverlayKind::Glyph, 0};
    overlay.orderCount = 3;
    EXPECT(renderer_detail::validOverlayFrame(overlay));
    EXPECT(overlay.order[0].kind == AuthoredOverlayKind::Sprite);
    EXPECT(overlay.order[1].kind == AuthoredOverlayKind::RoundedRect);
    EXPECT(overlay.order[2].kind == AuthoredOverlayKind::Glyph);
}

void MalformedOverlayReferencesFailValidation() {
    using namespace helengine::dreamcast;
    SpriteTexture texture{reinterpret_cast<void*>(1), 8, 4, 4};
    AuthoredOverlayFrame overlay{};
    overlay.roundedRects[0] = {0, 0, 10, 10, 2, 0, 0, {1, 1, 1, 1}, {0, 0, 0, 1}};
    overlay.roundedRectCount = 1;
    overlay.order[0] = {AuthoredOverlayKind::RoundedRect, 0};
    overlay.orderCount = 1;
    EXPECT(renderer_detail::validOverlayFrame(overlay));
    overlay.roundedRects[0].destinationWidth = 0;
    EXPECT(!renderer_detail::validOverlayFrame(overlay));

    overlay = AuthoredOverlayFrame{};
    overlay.glyphs[0] = {0, 0, 8, 10, 0, 0, 1.1f, 0.5f, &texture, {1, 1, 1, 1}};
    overlay.glyphCount = 1;
    overlay.order[0] = {AuthoredOverlayKind::Glyph, 0};
    overlay.orderCount = 1;
    EXPECT(!renderer_detail::validOverlayFrame(overlay));
    overlay.glyphs[0].sourceU1 = 0.5f;
    overlay.glyphs[0].texture = nullptr;
    EXPECT(!renderer_detail::validOverlayFrame(overlay));
    overlay.glyphs[0].texture = &texture;
    overlay.order[0].index = 1;
    EXPECT(!renderer_detail::validOverlayFrame(overlay));
}

} // namespace

int main() {
    PacketConversionInitializesEveryField();
    PacketConversionMarksOnlyThirdVertexEndOfLine();
    ThreePacketSequenceHasExactlyOneEndOfLine();
    FixedPoseAuthoredWindingConversionIsExplicit();
    SimulationDeltaIsClampedToAvoidDebuggerJumps();
    DiagnosticSnapshotLayoutIsStableForEmulatorReads();
    ScreenTriangleDiagnosticUsesFrontFacingWinding();
    MixedOverlayValidationAcceptsAndPreservesThreeKinds();
    MalformedOverlayReferencesFailValidation();

    if (g_failures != 0) {
        std::cerr << g_failures << " assertion(s) failed\n";
        return 1;
    }
    std::cout << "DreamcastRasterSeamTests: all tests passed\n";
    return 0;
}
