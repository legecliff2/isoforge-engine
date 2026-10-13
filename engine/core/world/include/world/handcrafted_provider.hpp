#pragma once
// Deprecated: HandcraftedProvider was renamed FileChunkProvider (it now serves chunk files of
// any origin). This header and alias stay for existing callers; new code should include
// world/file_chunk_provider.hpp and use FileChunkProvider. (Not [[deprecated]] yet so builds
// with -Werror stay green until callers migrate.)
#include "world/file_chunk_provider.hpp"

namespace eng::world {
using HandcraftedProvider = FileChunkProvider;   // deprecated alias
} // namespace eng::world
