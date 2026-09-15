#pragma once

#include <string>

namespace helengine::dreamcast {

/// Converts one manifest-relative asset path into an absolute KOS ROMFS path.
///
/// The input is deliberately narrower than a filesystem path. It must contain
/// ordinary relative segments only; callers cannot escape /rd or select a host
/// drive through this seam.
std::string normalizeContentPath(const std::string& path);

} // namespace helengine::dreamcast
