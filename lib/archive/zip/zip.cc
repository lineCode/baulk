///
#include <bela/path.hpp>
#include <bela/endian.hpp>
#include <bela/algorithm.hpp>
#include <bela/bufio.hpp>
#include <bitset>
#include <bela/terminal.hpp>
#include "zipinternal.hpp"

namespace baulk::archive::zip {
// string cleaned avoid zip string contains 'NUL'
inline std::string stringCleaned(const void *data, size_t N) {
  auto p = reinterpret_cast<const char *>(data);
  auto pos = memchr(data, 0, N);
  if (pos == nullptr) {
    return std::string(p, N);
  }
  N = reinterpret_cast<const char *>(pos) - p;
  return std::string(p, N);
}
int findSignatureInBlock(const bela::Buffer &b) {
  for (auto i = static_cast<int>(b.size()) - directoryEndLen; i >= 0; i--) {
    if (b[i] == 'P' && b[i + 1] == 'K' && b[i + 2] == 0x05 && b[i + 3] == 0x06) {
      auto n = static_cast<int>(b[i + directoryEndLen - 2]) | (static_cast<int>(b[i + directoryEndLen - 1]) << 8);
      if (n + directoryEndLen + i <= static_cast<int>(b.size())) {
        return i;
      }
    }
  }
  return -1;
}
bool Reader::readDirectory64End(int64_t offset, directoryEnd &d, bela::error_code &ec) {
  uint8_t buffer[256];
  if (!ReadAt(buffer, directory64EndLen, offset, ec)) {
    return false;
  }
  bela::endian::LittenEndian b(buffer, directory64EndLen);
  if (auto sig = b.Read<uint32_t>(); sig != directory64EndSignature) {
    ec = bela::make_error_code(L"zip: not a valid zip file");
    return false;
  }
  b.Discard(12);
  d.diskNbr = b.Read<uint32_t>();    // number of this disk
  d.dirDiskNbr = b.Read<uint32_t>(); // number of the disk with the start of the central directory
                                     // total number of entries in the central directory on this disk
  d.dirRecordsThisDisk = b.Read<uint64_t>();
  d.directoryRecords = b.Read<uint64_t>(); // total number of entries in the central directory
  d.directorySize = b.Read<uint64_t>();    // size of the central directory
  // offset of start of central directory with respect to the starting disk number
  d.directoryOffset = b.Read<uint64_t>();
  return true;
}

int64_t Reader::findDirectory64End(int64_t directoryEndOffset, bela::error_code &ec) {
  auto locOffset = directoryEndOffset - directory64LocLen;
  if (locOffset < 0) {
    return -1;
  }
  uint8_t buffer[256];
  if (!ReadAt(buffer, directory64LocLen, locOffset, ec)) {
    return -1;
  }
  bela::endian::LittenEndian b(buffer, directory64LocLen);
  if (auto sig = b.Read<uint32_t>(); sig != directory64LocSignature) {
    return -1;
  }
  if (b.Read<uint32_t>() != 0) {
    return -1;
  }
  auto p = b.Read<uint64_t>();
  if (b.Read<uint32_t>() != 1) {
    return -1;
  }
  return static_cast<int64_t>(p);
}

// github.com\klauspost\compress@v1.11.3\zip\reader.go
bool Reader::readDirectoryEnd(directoryEnd &d, bela::error_code &ec) {
  bela::Buffer buffer(16 * 1024);
  int64_t directoryEndOffset = 0;
  constexpr int64_t offrange[] = {1024, 65 * 1024};
  bela::endian::LittenEndian b;
  for (size_t i = 0; i < bela::ArrayLength(offrange); i++) {
    auto blen = offrange[i];
    if (blen > size) {
      blen = size;
    }
    buffer.grow(blen);
    if (!ReadAt(buffer.data(), blen, size - blen, ec)) {
      return false;
    }
    buffer.size() = blen;
    if (auto p = findSignatureInBlock(buffer); p >= 0) {
      b.Reset(reinterpret_cast<const char *>(buffer.data()) + p, blen - p);
      directoryEndOffset = size - blen + p;
      break;
    }
    if (i == 1 || blen == size) {
      ec = bela::make_error_code(L"zip: not a valid zip file");
      return false;
    }
  }
  if (b.Discard(4) < 18) {
    ec = bela::make_error_code(L"zip: not a valid zip file");
    return false;
  }
  d.diskNbr = b.Read<uint16_t>();
  d.dirDiskNbr = b.Read<uint16_t>();
  d.dirRecordsThisDisk = b.Read<uint16_t>();
  d.directoryRecords = b.Read<uint16_t>();
  d.directorySize = b.Read<uint32_t>();
  d.directoryOffset = b.Read<uint32_t>();
  auto commentLen = b.Read<uint16_t>();
  if (static_cast<size_t>(commentLen) > b.Size()) {
    ec = bela::make_error_code(L"zip: invalid comment length");
    return false;
  }
  comment = stringCleaned(b.Data(), commentLen);
  if (d.directoryRecords == 0xFFFF || d.directorySize == 0xFFFF || d.directoryOffset == 0xFFFFFFFF) {
    ec.clear();
    auto p = findDirectory64End(directoryEndOffset, ec);
    if (!ec && p > 0) {
      readDirectory64End(p, d, ec);
    }
    if (ec) {
      return false;
    }
  }
  if (auto o = static_cast<int64_t>(d.directoryOffset); o < 0 || 0 >= size) {
    ec = bela::make_error_code(L"zip: not a valid zip file");
    return false;
  }
  return true;
}
using bufioReader = bela::bufio::Reader<4096>;

constexpr uint32_t SizeMin = 0xFFFFFFFFu;
constexpr uint64_t OffsetMin = 0xFFFFFFFFull;

// Thanks github.com\klauspost\compress@v1.11.3\zip\reader.go

/*
        central file header signature   4 bytes  (0x02014b50)
        version made by                 2 bytes
        version needed to extract       2 bytes
        general purpose bit flag        2 bytes
        compression method              2 bytes
        last mod file time              2 bytes
        last mod file date              2 bytes
        crc-32                          4 bytes
        compressed size                 4 bytes
        uncompressed size               4 bytes
        file name length                2 bytes
        extra field length              2 bytes
        file comment length             2 bytes
        disk number start               2 bytes
        internal file attributes        2 bytes
        external file attributes        4 bytes
        relative offset of local header 4 bytes

        file name (variable size)
        extra field (variable size)
        file comment (variable size)

*/

bool readDirectoryHeader(bufioReader &br, Buffer &buffer, File &file, bela::error_code &ec) {
  uint8_t buf[directoryHeaderLen];
  if (br.ReadFull(buf, sizeof(buf), ec) != sizeof(buf)) {
    return false;
  }
  bela::endian::LittenEndian b(buf, sizeof(buf));
  if (auto n = static_cast<int>(b.Read<uint32_t>()); n != directoryHeaderSignature) {
    ec = bela::make_error_code(L"zip: not a valid zip file");
    return false;
  }
  file.cversion = b.Read<uint16_t>();
  file.rversion = b.Read<uint16_t>();
  file.flags = b.Read<uint16_t>();
  file.method = b.Read<uint16_t>();
  auto dosTime = b.Read<uint16_t>();
  auto dosDate = b.Read<uint16_t>();
  file.crc32sum = b.Read<uint32_t>();
  file.compressedSize = b.Read<uint32_t>();
  file.uncompressedSize = b.Read<uint32_t>();
  auto filenameLen = b.Read<uint16_t>();
  auto extraLen = b.Read<uint16_t>();
  auto commentLen = b.Read<uint16_t>();
  /* skip
        disk number start               2 bytes
        internal file attributes        2 bytes
  */
  b.Discard(4);
  auto externalAttrs = b.Read<uint32_t>();
  file.position = b.Read<uint32_t>();
  auto totallen = filenameLen + extraLen + commentLen;
  buffer.grow(totallen);
  if (br.ReadFull(buffer.data(), totallen, ec) != totallen) {
    return false;
  }
  file.name = stringCleaned(buffer.data(), filenameLen);
  if (commentLen != 0) {
    file.comment = stringCleaned(buffer.data() + filenameLen + extraLen, commentLen);
  }
  file.mode = resolveFileMode(file, externalAttrs);
  auto needUSize = file.uncompressedSize == SizeMin;
  auto needSize = file.compressedSize == SizeMin;
  auto needOffset = file.position == OffsetMin;
  bela::Time modified;
  bela::endian::LittenEndian extra(buffer.data() + filenameLen, static_cast<size_t>(extraLen));
  for (; extra.Size() >= 4;) {
    auto fieldTag = extra.Read<uint16_t>();
    auto fieldSize = static_cast<int>(extra.Read<uint16_t>());
    if (extra.Size() < static_cast<size_t>(fieldSize)) {
      break;
    }
    auto fb = extra.Sub(fieldSize);
    if (fieldTag == zip64ExtraID) {
      if (needUSize) {
        needUSize = false;
        if (fb.Size() < 8) {
          ec = bela::make_error_code(L"zip: not a valid zip file");
          return false;
        }
        file.uncompressedSize = fb.Read<uint64_t>();
      }
      if (needSize) {
        needSize = false;
        if (fb.Size() < 8) {
          ec = bela::make_error_code(L"zip: not a valid zip file");
          return false;
        }
        file.compressedSize = fb.Read<uint64_t>();
      }
      if (needOffset) {
        needOffset = false;
        if (fb.Size() < 8) {
          ec = bela::make_error_code(L"zip: not a valid zip file");
          return false;
        }
        file.position = fb.Read<uint64_t>();
      }
      continue;
    }
    if (fieldTag == ntfsExtraID) {
      if (fb.Size() < 4) {
        continue;
      }
      fb.Discard(4);
      for (; fb.Size() >= 4;) {
        auto attrTag = fb.Read<uint16_t>();
        auto attrSize = fb.Read<uint16_t>();
        if (fb.Size() < attrSize) {
          break;
        }
        auto ab = fb.Sub(attrSize);
        if (attrTag != 1 || attrSize != 24) {
          break;
        }
        modified = bela::FromWindowsPreciseTime(ab.Read<uint64_t>());
      }
      continue;
    }
    if (fieldTag == unixExtraID || fieldTag == infoZipUnixExtraID) {
      if (fb.Size() < 8) {
        continue;
      }
      fb.Discard(4);
      file.time = bela::FromUnixSeconds(static_cast<int64_t>(fb.Read<uint32_t>()));
      continue;
    }
    if (fieldTag == extTimeExtraID) {
      if (fb.Size() < 5) {
        continue;
      }
      auto flags = fb.Pick();
      if ((flags & 0x1) != 0) {
        modified = bela::FromUnixSeconds(static_cast<int64_t>(fb.Read<uint32_t>()));
        continue;
      }
      if ((flags & 0x2) != 0) {
        // atime: access time
        continue;
      }
      if ((flags & 0x4) != 0) {
        // ctime: create time
        continue;
      }
      continue;
    }
    if (fieldTag == infoZipUnicodePathID) {
      /*
       (UPath) 0x7075        Short       tag for this extra block type ("up")
         TSize         Short       total data size for this block
         Version       1 byte      version of this extra field, currently 1
         NameCRC32     4 bytes     File Name Field CRC32 Checksum
         UnicodeName   Variable    UTF-8 version of the entry File Name
      */
      if (fb.Size() < 7 || (file.flags & 0x800) != 0) {
        continue;
      }
      auto size = fb.Read<uint16_t>();
      auto ver = fb.Pick();
      auto crc32val = fb.Read<uint32_t>();
      file.flags |= 0x800;
      file.name = stringCleaned(fb.Data<char>(), fb.Size());
      continue;
    }
    // https://www.winzip.com/win/en/aes_info.html
    if (fieldTag == winzipAesExtraID) {
      if (fb.Size() < 7) {
        continue;
      }
      file.aesVersion = fb.Read<uint16_t>();
      fb.Discard(2); // VendorID 'AE'
      file.aesStrength = fb.Pick();
      file.method = fb.Read<uint16_t>();
      continue;
    }
    ///
  }
  file.time = bela::FromDosDateTime(dosDate, dosTime);
  if (bela::ToUnixSeconds(modified) != 0) {
    file.time = modified;
  }
  if (needSize || needOffset) {
    ec = bela::make_error_code(L"zip: not a valid zip file");
    return false;
  }
  return true;
}

bool Reader::Initialize(bela::error_code &ec) {
  LARGE_INTEGER li;
  if (GetFileSizeEx(fd, &li) != TRUE) {
    ec = bela::make_system_error_code(L"GetFileSizeEx: ");
    return false;
  }
  size = li.QuadPart;
  directoryEnd d;
  if (!readDirectoryEnd(d, ec)) {
    return false;
  }
  if (d.directoryRecords > static_cast<uint64_t>(size) / fileHeaderLen) {
    ec = bela::make_error_code(ErrGeneral, L"zip: TOC declares impossible ", d.directoryRecords, L" files in ", size,
                               L" byte zip");
    return false;
  }
  files.reserve(d.directoryRecords);
  if (!PositionAt(d.directoryOffset, ec)) {
    return false;
  }
  // 64K avoid group
  Buffer buffer(64 * 1024);
  bufioReader br(fd);
  for (uint64_t i = 0; i < d.directoryRecords; i++) {
    File file;
    if (!readDirectoryHeader(br, buffer, file, ec)) {
      return false;
    }
    uncompressedSize += file.uncompressedSize;
    compressedSize += file.compressedSize;
    files.emplace_back(std::move(file));
  }
  return true;
}

bool Reader::OpenReader(std::wstring_view file, bela::error_code &ec) {
  if (fd != INVALID_HANDLE_VALUE) {
    ec = bela::make_error_code(L"The file has been opened, the function cannot be called repeatedly");
    return false;
  }
  fd = CreateFileW(file.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                   FILE_ATTRIBUTE_NORMAL, nullptr);
  if (fd == INVALID_HANDLE_VALUE) {
    ec = bela::make_system_error_code();
    return false;
  }
  needClosed = true;
  return Initialize(ec);
}

bool Reader::OpenReader(HANDLE nfd, int64_t sz, bela::error_code &ec) {
  if (fd != INVALID_HANDLE_VALUE) {
    ec = bela::make_error_code(L"The file has been opened, the function cannot be called repeatedly");
    return false;
  }
  fd = nfd;
  size = sz;
  return Initialize(ec);
}
} // namespace baulk::archive::zip