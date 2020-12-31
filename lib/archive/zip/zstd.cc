///
#include "zipinternal.hpp"
#include <zstd.h>

namespace baulk::archive::zip {
// zstd
// https://github.com/facebook/zstd/blob/dev/examples/streaming_decompression.c
bool Reader::decompressZstd(const File &file, const Receiver &receiver, int64_t &decompressed,
                            bela::error_code &ec) const {
  const auto bufferOutSize = ZSTD_DStreamOutSize();
  const auto bufferInSize = ZSTD_DStreamInSize();
  Buffer outbuf(bufferOutSize);
  Buffer inbuf(bufferInSize);
  auto zds = ZSTD_createDCtx();
  if (zds == nullptr) {
    ec = bela::make_error_code(L"ZSTD_createDStream() out of memory");
    return false;
  }
  auto closer = bela::finally([&] { ZSTD_freeDCtx(zds); });
  auto csize = file.compressedSize;
  uint32_t crc32val = 0;
  while (csize != 0) {
    auto minsize = (std::min)(csize, static_cast<uint64_t>(bufferInSize));
    if (!ReadFull(inbuf.data(), static_cast<size_t>(minsize), ec)) {
      return false;
    }
    ZSTD_inBuffer in{inbuf.data(), minsize, 0};
    ZSTD_outBuffer out{outbuf.data(), bufferOutSize, 0};
    while (in.pos < in.size) {
      auto result = ZSTD_decompressStream(zds, &out, &in);
      if (ZSTD_isError(result)) {
        ec = bela::make_error_code(1, bela::ToWide(ZSTD_getErrorName(result)));
        return false;
      }
      crc32val = baulk_crc32_update(crc32val, reinterpret_cast<const uint8_t *>(out.dst), out.pos);
      if (!receiver(out.dst, out.pos)) {
        ec = bela::make_error_code(ErrCanceled, L"canceled");
        return false;
      }
      decompressed += out.pos;
    }
    csize -= minsize;
  }
  if (crc32val != file.crc32) {
    ec = bela::make_error_code(1, L"crc32 want ", file.crc32, L" got ", crc32val, L" not match");
    return false;
  }
  return true;
}
} // namespace baulk::archive::zip