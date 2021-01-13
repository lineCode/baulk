//
#ifndef BAULK_ARCHIVE_TAR_INTERNAL_HPP
#define BAULK_ARCHIVE_TAR_INTERNAL_HPP
#include <tar.hpp>

namespace baulk::archive::tar {
// https://www.mkssoftware.com/docs/man4/tar.4.asp
/*  File Header (512 bytes)
 *  Offst Size Field
 *      Pre-POSIX Header
 *  0     100  File name
 *  100   8    File mode
 *  108   8    Owner's numeric user ID
 *  116   8    Group's numeric user ID
 *  124   12   File size in bytes (octal basis)
 *  136   12   Last modification time in numeric Unix time format (octal)
 *  148   8    Checksum for header record
 *  156   1    Type flag
 *  157   100  Name of linked file
 *      UStar Format
 *  257   6    UStar indicator "ustar"
 *  263   2    UStar version "00"
 *  265   32   Owner user name
 *  297   32   Owner group name
 *  329   8    Device major number
 *  337   8    Device minor number
 *  345   155  Filename prefix
 */

// Type '0' indicates a regular file.
constexpr char TypeReg = '0';
constexpr char TypeRegA = '\x00'; // Deprecated: Use TypeReg instead.

// Type '1' to '6' are header-only flags and may not have a data body.
constexpr char TypeLink = '1';    // Hard link
constexpr char TypeSymlink = '2'; // Symbolic link
constexpr char TypeChar = '3';    // Character device node
constexpr char TypeBlock = '4';   // Block device node
constexpr char TypeDir = '5';     // Directory
constexpr char TypeFifo = '6';    // FIFO node

// Type '7' is reserved.
constexpr char TypeCont = '7';

// Type 'x' is used by the PAX format to store key-value records that
// are only relevant to the next file.
// This package transparently handles these types.
constexpr char TypeXHeader = 'x';

// Type 'g' is used by the PAX format to store key-value records that
// are relevant to all subsequent files.
// This package only supports parsing and composing such headers,
// but does not currently support persisting the global state across files.
constexpr char TypeXGlobalHeader = 'g';

// Type 'S' indicates a sparse file in the GNU format.
constexpr char TypeGNUSparse = 'S';

// Types 'L' and 'K' are used by the GNU format for a meta file
// used to store the path or link name for the next file.
// This package transparently handles these types.
constexpr char TypeGNULongName = 'L';
constexpr char TypeGNULongLink = 'K';

constexpr char magicGNU[] = {'u', 's', 't', 'a', 'r', ' '};
constexpr char versionGNU[] = {' ', 0x00};
constexpr char magicUSTAR[] = {'u', 's', 't', 'a', 'r', 0x00};
constexpr char versionUSTAR[] = {'0', '0'};
constexpr char trailerSTAR[] = {'t', 'a', 'r', 0x00};

constexpr size_t blockSize = 512;  // Size of each block in a tar stream
constexpr size_t nameSize = 100;   // Max length of the name field in USTAR format
constexpr size_t prefixSize = 155; // Max length of the prefix field in USTAR format

constexpr size_t outsize = 64 * 1024;
constexpr size_t insize = 32 * 1024;

/*
 * Structure of GNU tar header
 */
struct gnu_sparse {
  char offset[12];
  char numbytes[12];
};

struct gnutar_header {
  char name[100];
  char mode[8];
  char uid[8];
  char gid[8];
  char size[12];
  char mtime[12];
  char checksum[8];
  char typeflag[1];
  char linkname[100];
  char magic[8]; /* "ustar  \0" (note blank/blank/null at end) */
  char uname[32];
  char gname[32];
  char rdevmajor[8];
  char rdevminor[8];
  char atime[12];
  char ctime[12];
  char offset[12];
  char longnames[4];
  char unused[1];
  gnu_sparse sparse[4];
  char isextended[1];
  char realsize[12];
  /*
   * Old GNU format doesn't use POSIX 'prefix' field; they use
   * the 'L' (longname) entry instead.
   */
};

enum tar_format {
  FormatUnknown = 0, // unknown
  FormatV7 = 1,
  FormatUSTAR = 2,
  FormatPAX = 4,
  FormatGNU = 8,
  FormatSTAR = 16,
  FormatMax = 32,
};

} // namespace baulk::archive::tar

#endif