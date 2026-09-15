#include "platform/dreamcast/DreamcastBootHost.hpp"

int main() {
    helengine::dreamcast::HostConfig config{};

#if defined(HELENGINE_SCENE_CLEAR)
    config.scene = helengine::dreamcast::HostScene::ClearOnly;
#elif defined(HELENGINE_SCENE_TRIANGLE)
    config.scene = helengine::dreamcast::HostScene::ScreenTriangle;
#else
    config.scene = helengine::dreamcast::HostScene::Cube;
#endif

#if defined(HELENGINE_STATIC)
    config.animate = false;
#endif
#if defined(HELENGINE_DISABLE_CULLING)
    config.disableCulling = true;
#endif
#if defined(HELENGINE_REVERSE_TRIANGLES)
    config.reverseTriangleOrder = true;
#endif
#if defined(HELENGINE_NEAR_PLANE_PROBE)
    config.nearPlaneProbe = true;
#endif
#if defined(HELENGINE_SCREEN_EDGE_PROBE)
    config.screenEdgeProbe = true;
#endif
#if defined(HELENGINE_STOP_AFTER_SECONDS)
    config.stopAfterSeconds = HELENGINE_STOP_AFTER_SECONDS;
#endif

    return helengine::dreamcast::runDreamcastHost(config);
}
