#pragma once
#include "AsePalette.h"
#include <objidl.h>

namespace ase {

// Read failures retain their HRESULT; malformed content is reported separately by Parse.
HRESULT ReadStream(IStream* stream, std::vector<std::byte>& bytes);

} // namespace ase
