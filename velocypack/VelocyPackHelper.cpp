////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Michael Hackstein
////////////////////////////////////////////////////////////////////////////////

#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <velocypack/AttributeTranslator.h>
#include <velocypack/Collection.h>
#include <velocypack/Dumper.h>
#include <velocypack/Iterator.h>
#include <velocypack/Options.h>
#include <velocypack/Slice.h>
#include <velocypack/StringRef.h>
#include <velocypack/velocypack-aliases.h>
#include <velocypack/velocypack-common.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>

//#include "Basics/operating-system.h"

#ifdef TRI_HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "./VelocyPackHelper.h"

extern "C" {
unsigned long long XXH64(const void* input, size_t length,
                         unsigned long long seed);
}

using namespace arangodb;
using VelocyPackHelper = arangodb::basics::VelocyPackHelper;

namespace {

static arangodb::velocypack::StringRef const idRef("id");
static arangodb::velocypack::StringRef const cidRef("cid");

static std::unique_ptr<VPackAttributeTranslator> translator;
static std::unique_ptr<VPackCustomTypeHandler> customTypeHandler;
static VPackOptions optionsWithUniquenessCheck;

// statically computed table of type weights
// the weight for type MinKey must be lowest, the weight for type MaxKey must be
// highest the table contains a special value -50 to indicate that the value is
// an external which must be resolved further the type Custom has the same
// weight as the String type, because the Custom type is used to store _id
// (which is a string)
static int8_t const typeWeights[256] = {
    0 /* 0x00 */,   4 /* 0x01 */,  4 /* 0x02 */, 4 /* 0x03 */,  4 /* 0x04 */,
    4 /* 0x05 */,   4 /* 0x06 */,  4 /* 0x07 */, 4 /* 0x08 */,  4 /* 0x09 */,
    5 /* 0x0a */,   5 /* 0x0b */,  5 /* 0x0c */, 5 /* 0x0d */,  5 /* 0x0e */,
    5 /* 0x0f */,   5 /* 0x10 */,  5 /* 0x11 */, 5 /* 0x12 */,  4 /* 0x13 */,
    5 /* 0x14 */,   0 /* 0x15 */,  0 /* 0x16 */, -1 /* 0x17 */, 0 /* 0x18 */,
    1 /* 0x19 */,   1 /* 0x1a */,  2 /* 0x1b */, 2 /* 0x1c */,  -50 /* 0x1d */,
    -99 /* 0x1e */, 99 /* 0x1f */, 2 /* 0x20 */, 2 /* 0x21 */,  2 /* 0x22 */,
    2 /* 0x23 */,   2 /* 0x24 */,  2 /* 0x25 */, 2 /* 0x26 */,  2 /* 0x27 */,
    2 /* 0x28 */,   2 /* 0x29 */,  2 /* 0x2a */, 2 /* 0x2b */,  2 /* 0x2c */,
    2 /* 0x2d */,   2 /* 0x2e */,  2 /* 0x2f */, 2 /* 0x30 */,  2 /* 0x31 */,
    2 /* 0x32 */,   2 /* 0x33 */,  2 /* 0x34 */, 2 /* 0x35 */,  2 /* 0x36 */,
    2 /* 0x37 */,   2 /* 0x38 */,  2 /* 0x39 */, 2 /* 0x3a */,  2 /* 0x3b */,
    2 /* 0x3c */,   2 /* 0x3d */,  2 /* 0x3e */, 2 /* 0x3f */,  3 /* 0x40 */,
    3 /* 0x41 */,   3 /* 0x42 */,  3 /* 0x43 */, 3 /* 0x44 */,  3 /* 0x45 */,
    3 /* 0x46 */,   3 /* 0x47 */,  3 /* 0x48 */, 3 /* 0x49 */,  3 /* 0x4a */,
    3 /* 0x4b */,   3 /* 0x4c */,  3 /* 0x4d */, 3 /* 0x4e */,  3 /* 0x4f */,
    3 /* 0x50 */,   3 /* 0x51 */,  3 /* 0x52 */, 3 /* 0x53 */,  3 /* 0x54 */,
    3 /* 0x55 */,   3 /* 0x56 */,  3 /* 0x57 */, 3 /* 0x58 */,  3 /* 0x59 */,
    3 /* 0x5a */,   3 /* 0x5b */,  3 /* 0x5c */, 3 /* 0x5d */,  3 /* 0x5e */,
    3 /* 0x5f */,   3 /* 0x60 */,  3 /* 0x61 */, 3 /* 0x62 */,  3 /* 0x63 */,
    3 /* 0x64 */,   3 /* 0x65 */,  3 /* 0x66 */, 3 /* 0x67 */,  3 /* 0x68 */,
    3 /* 0x69 */,   3 /* 0x6a */,  3 /* 0x6b */, 3 /* 0x6c */,  3 /* 0x6d */,
    3 /* 0x6e */,   3 /* 0x6f */,  3 /* 0x70 */, 3 /* 0x71 */,  3 /* 0x72 */,
    3 /* 0x73 */,   3 /* 0x74 */,  3 /* 0x75 */, 3 /* 0x76 */,  3 /* 0x77 */,
    3 /* 0x78 */,   3 /* 0x79 */,  3 /* 0x7a */, 3 /* 0x7b */,  3 /* 0x7c */,
    3 /* 0x7d */,   3 /* 0x7e */,  3 /* 0x7f */, 3 /* 0x80 */,  3 /* 0x81 */,
    3 /* 0x82 */,   3 /* 0x83 */,  3 /* 0x84 */, 3 /* 0x85 */,  3 /* 0x86 */,
    3 /* 0x87 */,   3 /* 0x88 */,  3 /* 0x89 */, 3 /* 0x8a */,  3 /* 0x8b */,
    3 /* 0x8c */,   3 /* 0x8d */,  3 /* 0x8e */, 3 /* 0x8f */,  3 /* 0x90 */,
    3 /* 0x91 */,   3 /* 0x92 */,  3 /* 0x93 */, 3 /* 0x94 */,  3 /* 0x95 */,
    3 /* 0x96 */,   3 /* 0x97 */,  3 /* 0x98 */, 3 /* 0x99 */,  3 /* 0x9a */,
    3 /* 0x9b */,   3 /* 0x9c */,  3 /* 0x9d */, 3 /* 0x9e */,  3 /* 0x9f */,
    3 /* 0xa0 */,   3 /* 0xa1 */,  3 /* 0xa2 */, 3 /* 0xa3 */,  3 /* 0xa4 */,
    3 /* 0xa5 */,   3 /* 0xa6 */,  3 /* 0xa7 */, 3 /* 0xa8 */,  3 /* 0xa9 */,
    3 /* 0xaa */,   3 /* 0xab */,  3 /* 0xac */, 3 /* 0xad */,  3 /* 0xae */,
    3 /* 0xaf */,   3 /* 0xb0 */,  3 /* 0xb1 */, 3 /* 0xb2 */,  3 /* 0xb3 */,
    3 /* 0xb4 */,   3 /* 0xb5 */,  3 /* 0xb6 */, 3 /* 0xb7 */,  3 /* 0xb8 */,
    3 /* 0xb9 */,   3 /* 0xba */,  3 /* 0xbb */, 3 /* 0xbc */,  3 /* 0xbd */,
    3 /* 0xbe */,   3 /* 0xbf */,  3 /* 0xc0 */, 3 /* 0xc1 */,  3 /* 0xc2 */,
    3 /* 0xc3 */,   3 /* 0xc4 */,  3 /* 0xc5 */, 3 /* 0xc6 */,  3 /* 0xc7 */,
    2 /* 0xc8 */,   2 /* 0xc9 */,  2 /* 0xca */, 2 /* 0xcb */,  2 /* 0xcc */,
    2 /* 0xcd */,   2 /* 0xce */,  2 /* 0xcf */, 2 /* 0xd0 */,  2 /* 0xd1 */,
    2 /* 0xd2 */,   2 /* 0xd3 */,  2 /* 0xd4 */, 2 /* 0xd5 */,  2 /* 0xd6 */,
    2 /* 0xd7 */,   0 /* 0xd8 */,  0 /* 0xd9 */, 0 /* 0xda */,  0 /* 0xdb */,
    0 /* 0xdc */,   0 /* 0xdd */,  0 /* 0xde */, 0 /* 0xdf */,  0 /* 0xe0 */,
    0 /* 0xe1 */,   0 /* 0xe2 */,  0 /* 0xe3 */, 0 /* 0xe4 */,  0 /* 0xe5 */,
    0 /* 0xe6 */,   0 /* 0xe7 */,  0 /* 0xe8 */, 0 /* 0xe9 */,  0 /* 0xea */,
    0 /* 0xeb */,   0 /* 0xec */,  0 /* 0xed */, 0 /* 0xee */,  0 /* 0xef */,
    3 /* 0xf0 */,   3 /* 0xf1 */,  3 /* 0xf2 */, 3 /* 0xf3 */,  3 /* 0xf4 */,
    3 /* 0xf5 */,   3 /* 0xf6 */,  3 /* 0xf7 */, 3 /* 0xf8 */,  3 /* 0xf9 */,
    3 /* 0xfa */,   3 /* 0xfb */,  3 /* 0xfc */, 3 /* 0xfd */,  3 /* 0xfe */,
    3 /* 0xff */,
};

}  // namespace

/// @brief turn off assembler optimizations in vpack
void VelocyPackHelper::disableAssemblerFunctions() {
  arangodb::velocypack::disableAssemblerFunctions();
}

/// @brief return the (global) attribute translator instance
arangodb::velocypack::AttributeTranslator* VelocyPackHelper::getTranslator() {
  return ::translator.get();
}

/// @brief return the (global) attribute translator instance
arangodb::velocypack::Options* VelocyPackHelper::optionsWithUniquenessCheck() {
  return &::optionsWithUniquenessCheck;
}

bool VelocyPackHelper::AttributeSorterBinary::operator()(
    std::string const& l, std::string const& r) const noexcept {
  // use binary comparison of attribute names
  size_t cmpLength = (std::min)(l.size(), r.size());
  int res = memcmp(l.data(), r.data(), cmpLength);
  if (res < 0) {
    return true;
  }
  if (res == 0) {
    return l.size() < r.size();
  }
  return false;
}

bool VelocyPackHelper::AttributeSorterBinaryStringRef::operator()(
    VPackStringRef const& l, VPackStringRef const& r) const noexcept {
  // use binary comparison of attribute names
  size_t cmpLength = (std::min)(l.size(), r.size());
  int res = memcmp(l.data(), r.data(), cmpLength);
  if (res < 0) {
    return true;
  }
  if (res == 0) {
    return l.size() < r.size();
  }
  return false;
}

size_t VelocyPackHelper::VPackHash::operator()(VPackSlice const& slice) const {
  return static_cast<size_t>(slice.normalizedHash());
}

size_t VelocyPackHelper::VPackStringHash::operator()(
    VPackSlice const& slice) const noexcept {
  return static_cast<size_t>(slice.hashString());
}

bool VelocyPackHelper::VPackStringEqual::operator()(VPackSlice const& lhs,
                                                    VPackSlice const& rhs) const
    noexcept {
  auto const lh = lhs.head();
  auto const rh = rhs.head();

  if (lh != rh) {
    return false;
  }

  VPackValueLength size;
  if (lh == 0xbf) {
    // long UTF-8 String
    size = static_cast<VPackValueLength>(
        velocypack::readIntegerFixed<VPackValueLength, 8>(lhs.begin() + 1));
    if (size != static_cast<VPackValueLength>(
                    velocypack::readIntegerFixed<VPackValueLength, 8>(
                        rhs.begin() + 1))) {
      return false;
    }
    return (memcmp(lhs.start() + 1 + 8, rhs.start() + 1 + 8,
                   static_cast<size_t>(size)) == 0);
  }

  size = static_cast<VPackValueLength>(lh - 0x40);
  return (memcmp(lhs.start() + 1, rhs.start() + 1, static_cast<size_t>(size)) ==
          0);
}

int VelocyPackHelper::compareNumberValues(VPackValueType lhsType,
                                          VPackSlice lhs, VPackSlice rhs) {
  if (lhsType == rhs.type()) {
    // both types are equal
    if (lhsType == VPackValueType::Int || lhsType == VPackValueType::SmallInt) {
      // use exact comparisons. no need to cast to double
      int64_t l = lhs.getIntUnchecked();
      int64_t r = rhs.getIntUnchecked();
      if (l == r) {
        return 0;
      }
      return (l < r ? -1 : 1);
    }

    if (lhsType == VPackValueType::UInt) {
      // use exact comparisons. no need to cast to double
      uint64_t l = lhs.getUIntUnchecked();
      uint64_t r = rhs.getUIntUnchecked();
      if (l == r) {
        return 0;
      }
      return (l < r ? -1 : 1);
    }
    // fallthrough to double comparison
  }

  double l = lhs.getNumericValue<double>();
  double r = rhs.getNumericValue<double>();
  if (l == r) {
    return 0;
  }
  return (l < r ? -1 : 1);
}

/// @brief compares two VelocyPack string values
int VelocyPackHelper::compareStringValues(char const* left, VPackValueLength nl,
                                          char const* right,
                                          VPackValueLength nr, bool useUTF8) {
  /*
int res;
if (useUTF8) {
  res = TRI_compare_utf8(left, static_cast<size_t>(nl), right,
                         static_cast<size_t>(nr));
} else {
  size_t len = static_cast<size_t>(nl < nr ? nl : nr);
  res = memcmp(left, right, len);
}

if (res != 0) {
  return (res < 0 ? -1 : 1);
}

// res == 0
if (nl == nr) {
  return 0;
}
// res == 0, but different string lengths
return (nl < nr ? -1 : 1);
*/
  return 0;
}

/// @brief returns a boolean sub-element, or a default if it is does not exist
bool VelocyPackHelper::getBooleanValue(VPackSlice const& slice,
                                       char const* name, bool defaultValue) {
  assert(slice.isObject());
  if (!slice.hasKey(name)) {
    return defaultValue;
  }
  VPackSlice const& sub = slice.get(name);

  if (sub.isBoolean()) {
    return sub.getBool();
  }

  return defaultValue;
}

bool VelocyPackHelper::getBooleanValue(VPackSlice const& slice,
                                       std::string const& name,
                                       bool defaultValue) {
  assert(slice.isObject());
  if (!slice.hasKey(name)) {
    return defaultValue;
  }
  VPackSlice const& sub = slice.get(name);

  if (sub.isBoolean()) {
    return sub.getBool();
  }

  return defaultValue;
}

/// @brief returns a string sub-element, or throws if <name> does not exist
/// or it is not a string
std::string VelocyPackHelper::checkAndGetStringValue(VPackSlice const& slice,
                                                     char const* name) {
  assert(slice.isObject());
  if (!slice.hasKey(name)) {
    std::string msg =
        "The attribute '" + std::string(name) + "' was not found.";
    // TRI_ERROR_BAD_PARAMETER
    throw std::runtime_error(msg);
  }
  VPackSlice const sub = slice.get(name);
  if (!sub.isString()) {
    std::string msg =
        "The attribute '" + std::string(name) + "' is not a string.";
    // TRI_ERROR_BAD_PARAMETER
    throw std::runtime_error(msg);
  }
  return sub.copyString();
}

/// @brief returns a string sub-element, or throws if <name> does not exist
/// or it is not a string
std::string VelocyPackHelper::checkAndGetStringValue(VPackSlice const& slice,
                                                     std::string const& name) {
  assert(slice.isObject());
  if (!slice.hasKey(name)) {
    std::string msg = "The attribute '" + name + "' was not found.";
    // TRI_ERROR_BAD_PARAMETER
    throw std::runtime_error(msg);
  }
  VPackSlice const sub = slice.get(name);
  if (!sub.isString()) {
    std::string msg = "The attribute '" + name + "' is not a string.";
    // TRI_ERROR_BAD_PARAMETER
    throw std::runtime_error(msg);
  }
  return sub.copyString();
}

void VelocyPackHelper::ensureStringValue(VPackSlice const& slice,
                                         std::string const& name) {
  assert(slice.isObject());
  if (!slice.hasKey(name)) {
    std::string msg = "The attribute '" + name + "' was not found.";
    // TRI_ERROR_BAD_PARAMETER
    throw std::runtime_error(msg);
  }
  VPackSlice const sub = slice.get(name);
  if (!sub.isString()) {
    std::string msg = "The attribute '" + name + "' is not a string.";
    // TRI_ERROR_BAD_PARAMETER
    throw std::runtime_error(msg);
  }
}

/*static*/ arangodb::velocypack::StringRef VelocyPackHelper::getStringRef(
    arangodb::velocypack::Slice slice,
    arangodb::velocypack::StringRef const& defaultValue) noexcept {
  return slice.isString() ? arangodb::velocypack::StringRef(slice)
                          : defaultValue;
}

/*static*/ arangodb::velocypack::StringRef VelocyPackHelper::getStringRef(
    arangodb::velocypack::Slice slice, std::string const& key,
    arangodb::velocypack::StringRef const& defaultValue) noexcept {
  if (slice.isExternal()) {
    slice = arangodb::velocypack::Slice(
        reinterpret_cast<uint8_t const*>(slice.getExternal()));
  }

  if (!slice.isObject() || !slice.hasKey(key)) {
    return defaultValue;
  }

  auto value = slice.get(key);

  return value.isString() ? arangodb::velocypack::StringRef(value)
                          : defaultValue;
}

/// @brief returns a string value, or the default value if it is not a string
std::string VelocyPackHelper::getStringValue(VPackSlice const& slice,
                                             std::string const& defaultValue) {
  if (!slice.isString()) {
    return defaultValue;
  }
  return slice.copyString();
}

/// @brief returns a string sub-element, or the default value if it does not
/// exist
/// or it is not a string
std::string VelocyPackHelper::getStringValue(VPackSlice slice, char const* name,
                                             std::string const& defaultValue) {
  if (slice.isExternal()) {
    slice = VPackSlice(reinterpret_cast<uint8_t const*>(slice.getExternal()));
  }
  assert(slice.isObject());
  if (!slice.hasKey(name)) {
    return defaultValue;
  }
  VPackSlice const sub = slice.get(name);
  if (!sub.isString()) {
    return defaultValue;
  }
  return sub.copyString();
}

/// @brief returns a string sub-element, or the default value if it does not
/// exist
/// or it is not a string
std::string VelocyPackHelper::getStringValue(VPackSlice slice,
                                             std::string const& name,
                                             std::string const& defaultValue) {
  if (slice.isExternal()) {
    slice = VPackSlice(reinterpret_cast<uint8_t const*>(slice.getExternal()));
  }
  assert(slice.isObject());
  if (!slice.hasKey(name)) {
    return defaultValue;
  }
  VPackSlice const sub = slice.get(name);
  if (!sub.isString()) {
    return defaultValue;
  }
  return sub.copyString();
}

// home/kapili3/arangodb/lib/Basics/StringUtils.cpp:1440
uint64_t uint64(std::string const& value) {
  try {
    return std::stoull(value, nullptr, 10);
  } catch (...) {
    return 0;
  }
}
uint64_t VelocyPackHelper::stringUInt64(VPackSlice const& slice) {
  if (slice.isString()) {
    // return arangodb::basics::StringUtils::uint64(slice.copyString());
    return uint64(slice.copyString());
  }
  if (slice.isNumber()) {
    return slice.getNumericValue<uint64_t>();
  }
  return 0;
}
/*
/// @brief parses a json file to VelocyPack
VPackBuilder VelocyPackHelper::velocyPackFromFile(std::string const& path) {
  size_t length;
  char* content = TRI_SlurpFile(path.c_str(), &length);
  if (content != nullptr) {
    auto guard = scopeGuard([&content]() { TRI_Free(content); });
    // The Parser might throw;
    VPackBuilder builder;
    VPackParser parser(builder);
    parser.parse(reinterpret_cast<uint8_t const*>(content), length);
    return builder;
  }
  THROW_ARANGO_EXCEPTION(TRI_errno());
}
*/
/*
static bool PrintVelocyPack(int fd, VPackSlice const& slice,
                            bool appendNewline) {
  if (slice.isNone()) {
    // sanity check
    return false;
  }

  arangodb::basics::StringBuffer buffer(false);
  arangodb::basics::VPackStringBufferAdapter bufferAdapter(
      buffer.stringBuffer());
  try {
    VPackDumper dumper(&bufferAdapter);
    dumper.dump(slice);
  } catch (...) {
    // Writing failed
    return false;
  }

  if (buffer.length() == 0) {
    // should not happen
    return false;
  }

  if (appendNewline) {
    // add the newline here so we only need one write operation in the ideal
    // case
    buffer.appendChar('\n');
  }

  char const* p = buffer.begin();
  size_t n = buffer.length();

  while (0 < n) {
    ssize_t m = TRI_WRITE(fd, p, static_cast<TRI_write_t>(n));

    if (m <= 0) {
      return false;
    }

    n -= m;
    p += m;
  }

  return true;
}
*/
/// @brief writes a VelocyPack to a file
bool VelocyPackHelper::velocyPackToFile(std::string const& filename,
                                        VPackSlice const& slice,
                                        bool syncFile) {
  std::string const tmp = filename + ".tmp";
  /*
    // remove a potentially existing temporary file
    if (TRI_ExistsFile(tmp.c_str())) {
      TRI_UnlinkFile(tmp.c_str());
    }

    int fd = TRI_CREATE(tmp.c_str(),
                        O_CREAT | O_TRUNC | O_EXCL | O_RDWR | TRI_O_CLOEXEC,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

    if (fd < 0) {
      TRI_set_errno(TRI_ERROR_SYS_ERROR);
      LOG_TOPIC("35198", WARN, arangodb::Logger::FIXME)
          << "cannot create json file '" << tmp << "': " << TRI_LAST_ERROR_STR;
      return false;
    }

    if (!PrintVelocyPack(fd, slice, true)) {
      TRI_CLOSE(fd);
      TRI_set_errno(TRI_ERROR_SYS_ERROR);
      LOG_TOPIC("549f4", WARN, arangodb::Logger::FIXME)
          << "cannot write to json file '" << tmp << "': " <<
  TRI_LAST_ERROR_STR; TRI_UnlinkFile(tmp.c_str()); return false;
    }

    if (syncFile) {
      LOG_TOPIC("0acab", TRACE, arangodb::Logger::FIXME)
          << "syncing tmp file '" << tmp << "'";

      if (!TRI_fsync(fd)) {
        TRI_CLOSE(fd);
        TRI_set_errno(TRI_ERROR_SYS_ERROR);
        LOG_TOPIC("fd628", WARN, arangodb::Logger::FIXME)
            << "cannot sync saved json '" << tmp << "': " << TRI_LAST_ERROR_STR;
        TRI_UnlinkFile(tmp.c_str());
        return false;
      }
    }

    int res = TRI_CLOSE(fd);

    if (res < 0) {
      TRI_set_errno(TRI_ERROR_SYS_ERROR);
      LOG_TOPIC("3f835", WARN, arangodb::Logger::FIXME)
          << "cannot close saved file '" << tmp << "': " << TRI_LAST_ERROR_STR;
      TRI_UnlinkFile(tmp.c_str());
      return false;
    }

    res = TRI_RenameFile(tmp.c_str(), filename.c_str());

    if (res != TRI_ERROR_NO_ERROR) {
      TRI_set_errno(res);
      LOG_TOPIC("7f5c9", WARN, arangodb::Logger::FIXME)
          << "cannot rename saved file '" << tmp << "' to '" << filename
          << "': " << TRI_LAST_ERROR_STR;
      TRI_UnlinkFile(tmp.c_str());

      return false;
    }

  #ifndef _WIN32
    if (syncFile) {
      // also sync target directory
      std::string const dir = TRI_Dirname(filename);
      fd = TRI_OPEN(dir.c_str(), O_RDONLY | TRI_O_CLOEXEC);
      if (fd < 0) {
        TRI_set_errno(TRI_ERROR_SYS_ERROR);
        LOG_TOPIC("fd84e", WARN, arangodb::Logger::FIXME)
            << "cannot sync directory '" << filename
            << "': " << TRI_LAST_ERROR_STR;
      } else {
        if (fsync(fd) < 0) {
          TRI_set_errno(TRI_ERROR_SYS_ERROR);
          LOG_TOPIC("6b8f6", WARN, arangodb::Logger::FIXME)
              << "cannot sync directory '" << filename
              << "': " << TRI_LAST_ERROR_STR;
        }
        res = TRI_CLOSE(fd);
        if (res < 0) {
          TRI_set_errno(TRI_ERROR_SYS_ERROR);
          LOG_TOPIC("7ceee", WARN, arangodb::Logger::FIXME)
              << "cannot close directory '" << filename
              << "': " << TRI_LAST_ERROR_STR;
        }
      }
    }
  #endif
  */
  return true;
}

VPackBuilder VelocyPackHelper::merge(VPackSlice const& lhs,
                                     VPackSlice const& rhs,
                                     bool nullMeansRemove, bool mergeObjects) {
  return VPackCollection::merge(lhs, rhs, mergeObjects, nullMeansRemove);
}

double VelocyPackHelper::toDouble(VPackSlice const& slice, bool& failed) {
  assert(!slice.isNone());

  failed = false;
  switch (slice.type()) {
    case VPackValueType::None:
    case VPackValueType::Null:
      return 0.0;
    case VPackValueType::Bool:
      return (slice.getBoolean() ? 1.0 : 0.0);
    case VPackValueType::Double:
    case VPackValueType::Int:
    case VPackValueType::UInt:
    case VPackValueType::SmallInt:
      return slice.getNumericValue<double>();
    case VPackValueType::String: {
      std::string tmp(slice.copyString());
      try {
        // try converting string to number
        return std::stod(tmp);
      } catch (...) {
        if (tmp.empty()) {
          return 0.0;
        }
        // conversion failed
      }
      break;
    }
    case VPackValueType::Array: {
      VPackValueLength const n = slice.length();

      if (n == 0) {
        return 0.0;
      } else if (n == 1) {
        return VelocyPackHelper::toDouble(slice.at(0).resolveExternal(),
                                          failed);
      }
      break;
    }
    case VPackValueType::External: {
      return VelocyPackHelper::toDouble(slice.resolveExternal(), failed);
    }
    case VPackValueType::Illegal:
    case VPackValueType::Object:
    case VPackValueType::UTCDate:
    case VPackValueType::MinKey:
    case VPackValueType::MaxKey:
    case VPackValueType::Binary:
    case VPackValueType::BCD:
    case VPackValueType::Custom:
      break;
  }

  failed = true;
  return 0.0;
}

// modify a VPack double value in place
void VelocyPackHelper::patchDouble(VPackSlice slice, double value) {
  assert(slice.isDouble());
  // get pointer to the start of the value
  uint8_t* p = const_cast<uint8_t*>(slice.begin());
  // skip one byte for the header and overwrite
#ifndef TRI_UNALIGNED_ACCESS
  // some architectures do not support unaligned writes, then copy bytewise
  uint64_t dv;
  memcpy(&dv, &value, sizeof(double));
  VPackValueLength vSize = sizeof(double);
  for (uint64_t x = dv; vSize > 0; vSize--) {
    *(++p) = x & 0xff;
    x >>= 8;
  }
#else
  // other platforms support unaligned writes
  // cppcheck-suppress *
  *reinterpret_cast<double*>(p + 1) = value;
#endif
}

bool VelocyPackHelper::hasNonClientTypes(VPackSlice input, bool checkExternals,
                                         bool checkCustom) {
  if (input.isExternal()) {
    return checkExternals;
  } else if (input.isCustom()) {
    return checkCustom;
  } else if (input.isObject()) {
    for (auto it : VPackObjectIterator(input, true)) {
      if (hasNonClientTypes(it.value, checkExternals, checkCustom)) {
        return true;
      }
    }
  } else if (input.isArray()) {
    for (VPackSlice it : VPackArrayIterator(input)) {
      if (hasNonClientTypes(it, checkExternals, checkCustom)) {
        return true;
      }
    }
  }
  return false;
}

void VelocyPackHelper::sanitizeNonClientTypes(VPackSlice input, VPackSlice base,
                                              VPackBuilder& output,
                                              VPackOptions const* options,
                                              bool sanitizeExternals,
                                              bool sanitizeCustom,
                                              bool allowUnindexed) {
  if (sanitizeExternals && input.isExternal()) {
    // recursively resolve externals
    sanitizeNonClientTypes(input.resolveExternal(), base, output, options,
                           sanitizeExternals, sanitizeCustom, allowUnindexed);
  } else if (sanitizeCustom && input.isCustom()) {
    if (options == nullptr || options->customTypeHandler == nullptr) {
      // TRI_ERROR_INTERNAL
      throw std::runtime_error(
          "cannot sanitize vpack without custom type handler");
    }
    std::string custom =
        options->customTypeHandler->toString(input, options, base);
    output.add(VPackValue(custom));
  } else if (input.isObject()) {
    output.openObject(allowUnindexed);
    for (auto it : VPackObjectIterator(input, true)) {
      VPackValueLength l;
      char const* p = it.key.getString(l);
      output.add(VPackValuePair(p, l, VPackValueType::String));
      sanitizeNonClientTypes(it.value, input, output, options,
                             sanitizeExternals, sanitizeCustom, allowUnindexed);
    }
    output.close();
  } else if (input.isArray()) {
    output.openArray(allowUnindexed);
    for (VPackSlice it : VPackArrayIterator(input)) {
      sanitizeNonClientTypes(it, input, output, options, sanitizeExternals,
                             sanitizeCustom, allowUnindexed);
    }
    output.close();
  } else {
    output.add(input);
  }
}

/// @brief extract the collection id from VelocyPack
uint64_t VelocyPackHelper::extractIdValue(VPackSlice const& slice) {
  if (!slice.isObject()) {
    return 0;
  }
  VPackSlice id = slice.get(::idRef);
  if (id.isNone()) {
    // pre-3.1 compatibility
    id = slice.get(::cidRef);
  }

  if (id.isString()) {
    // string cid, e.g. "9988488"
    VPackValueLength l;
    char const* p = id.getStringUnchecked(l);
    // Todo FIx me
    // return NumberUtils::atoi_zero<uint64_t>(p, p + l);
  } else if (id.isNumber()) {
    // numeric cid, e.g. 9988488
    return id.getNumericValue<uint64_t>();
  } else if (!id.isNone()) {
    // TRI_ERROR_BAD_PARAMETER
    throw std::runtime_error("invalid value for 'id' attribute");
  }

  assert(id.isNone());
  return 0;
}