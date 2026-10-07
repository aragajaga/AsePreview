#include "pch.h"
#include "AseStream.h"
#include <algorithm>
#include <new>

HRESULT ase::ReadStream(IStream* stream, std::vector<std::byte>& bytes) {
    if (!stream) {
        return E_POINTER;
    }

    bytes.clear();

    try {
        // Stat is advisory: also enforce the limit for streams without a known size.
        STATSTG stat{};
        if (SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart > MaxBytes) {
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        }

        LARGE_INTEGER start{};
        const HRESULT seek = stream->Seek(start, STREAM_SEEK_SET, nullptr);
        if (FAILED(seek) && seek != STG_E_INVALIDFUNCTION && seek != E_NOTIMPL) {
            return seek;
        }

        std::array<std::byte, 16384> chunk;
        for (;;) {
            ULONG read = 0;
            const HRESULT hr = stream->Read(chunk.data(), ULONG(chunk.size()), &read);

            if (FAILED(hr)) {
                return hr;
            }
            if (read > chunk.size()) {
                return E_UNEXPECTED;
            }
            if (read > MaxBytes - bytes.size()) {
                return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
            }

            bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + read);

            // Short S_OK reads are legal for custom streams; continue until end-of-stream.
            if (hr == S_FALSE || read == 0) {
                return S_OK;
            }
        }
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_FAIL;
    }
}
