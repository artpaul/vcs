#include <stdint.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _MSC_VER
#include <compat/msvc.h>
#else
#include <ctime>
#endif

#include "gdelta.h"
#include "gear_matrix.h"

namespace {

#define INIT_BUFFER_SIZE 128 * 1024
#define FPTYPE uint64_t
#define STRLOOK 16
#define STRLSTEP 2

#define PRINT_PERF 0
#define DEBUG_UNITS 0

#pragma pack(push, 1)
/*
 * ABI:
 *
 * VarInt<N>: more 1 | pval N [more| VarInt<7>]
 * DeltaHead: flag 1 | VarInt<6>
 * DeltaUnit: DeltaHead [DeltaHead.flag| VarInt<7>]
 *
 * VarInt <- Val, Offset = Val | VarInt[i].pval << Offset, Offset + VarInt[i]::N
 */
template <uint8_t FLAGLEN>
struct _DeltaHead {
  uint8_t flag : FLAGLEN;
  uint8_t more : 1;
  uint8_t length : (7 - FLAGLEN);
  static constexpr uint8_t lenbits = FLAGLEN;
};

using DeltaHeadUnit = _DeltaHead<1>;

struct VarIntPart {
  uint8_t more : 1;
  uint8_t subint : 7;
  static constexpr uint8_t lenbits = 7;
};

#pragma pack(pop)

struct DeltaUnitMem {
  uint8_t flag;
  uint64_t length;
  uint64_t offset;
};

// DeltaUnit/FlaggedVarInt: flag: 1, more: 1, len: 6
// VarInt: more: 1, len: 7
static_assert(sizeof(DeltaHeadUnit) == 1, "Expected DeltaHeads to be 1 byte");
static_assert(sizeof(VarIntPart) == 1, "Expected VarInt to be 1 byte");

struct BufferStreamDescriptor {
  uint8_t* buf;
  uint64_t cursor;
  uint64_t length;
  bool error{false};
};

struct ConstBufferStreamDescriptor {
  const uint8_t* buf;
  uint64_t cursor;
  uint64_t length;
  bool error{false};
};

void ensure_stream_length(BufferStreamDescriptor& stream, size_t length) {
  if (length > stream.length) {
    stream.buf = (uint8_t*)std::realloc(stream.buf, length);
    stream.length = length;
  }
}

template <typename T>
void write_field(BufferStreamDescriptor& buffer, const T& field) {
  ensure_stream_length(buffer, buffer.cursor + sizeof(T));
  std::memcpy(buffer.buf + buffer.cursor, &field, sizeof(T));
  buffer.cursor += sizeof(T);
  // TODO: check bounds (buffer->length)?
}

template <typename B, typename T>
void read_field(B& buffer, T& field) {
  if (buffer.cursor + sizeof(T) > buffer.length) {
    buffer.error = true;
    return;
  }
  std::memcpy(&field, buffer.buf + buffer.cursor, sizeof(T));
  buffer.cursor += sizeof(T);
}

void stream_into(BufferStreamDescriptor& dest, BufferStreamDescriptor& src,
                 size_t length) {
  ensure_stream_length(dest, dest.cursor + length);
  std::memcpy(dest.buf + dest.cursor, src.buf + src.cursor, length);
  dest.cursor += length;
  src.cursor += length;
}

void stream_from(BufferStreamDescriptor& dest,
                 const BufferStreamDescriptor& src, size_t src_cursor,
                 size_t length) {
  ensure_stream_length(dest, dest.cursor + length);
  std::memcpy(dest.buf + dest.cursor, src.buf + src_cursor, length);
  dest.cursor += length;
}

bool stream_from_const(BufferStreamDescriptor& dest,
                       const ConstBufferStreamDescriptor& src,
                       size_t src_cursor, size_t length) {
  if ((dest.cursor + length) > dest.length) {
    dest.error = true;
    return false;
  }
  if ((src.cursor + length) > src.length) {
    return false;
  }
  std::memcpy(dest.buf + dest.cursor, src.buf + src_cursor, length);
  dest.cursor += length;
  return true;
}

bool stream_into_const(BufferStreamDescriptor& dest,
                       ConstBufferStreamDescriptor& src, size_t length) {
  if ((dest.cursor + length) > dest.length) {
    dest.error = true;
    return false;
  }
  if ((src.cursor + length) > src.length) {
    src.error = true;
    return false;
  }
  std::memcpy(dest.buf + dest.cursor, src.buf + src.cursor, length);
  dest.cursor += length;
  src.cursor += length;
  return true;
}

void write_concat_buffer(BufferStreamDescriptor& dest,
                         const BufferStreamDescriptor& src) {
  ensure_stream_length(dest, dest.cursor + src.cursor + 1);
  std::memcpy(dest.buf + dest.cursor, src.buf, src.cursor);
  dest.cursor += src.cursor;
}

template <typename B>
uint64_t read_varint(B& buffer) {
  VarIntPart vi;
  uint64_t val = 0;
  uint8_t offset = 0;
  do {
    read_field(buffer, vi);
    if (buffer.error) {
      return 0;
    }
    val |= vi.subint << offset;
    offset += VarIntPart::lenbits;
  } while (vi.more);
  return val;
}

template <typename B>
void read_unit(B& buffer, DeltaUnitMem& unit) {
  DeltaHeadUnit head;
  read_field(buffer, head);
  if (buffer.error) {
    return;
  }

  unit.flag = head.flag;
  unit.length = head.length;
  if (head.more) {
    unit.length = read_varint(buffer) << DeltaHeadUnit::lenbits | unit.length;
  }
  if (head.flag) {
    unit.offset = read_varint(buffer);
  }
}

static constexpr uint8_t varint_mask = ((1 << VarIntPart::lenbits) - 1);
static constexpr uint8_t head_varint_mask = ((1 << DeltaHeadUnit::lenbits) - 1);

void write_varint(BufferStreamDescriptor& buffer, uint64_t val) {
  VarIntPart vi;
  do {
    vi.subint = val & varint_mask;
    val >>= VarIntPart::lenbits;
    if (val == 0) {
      vi.more = 0;
      write_field(buffer, vi);
      break;
    }
    vi.more = 1;
    write_field(buffer, vi);
  } while (1);
}

void write_unit(BufferStreamDescriptor& buffer, const DeltaUnitMem& unit) {
  // TODO: Abort if length 0?
#if DEBUG_UNITS
  fprintf(stderr, "Writing unit %d %zu %zu\n", unit.flag, unit.length,
          unit.offset);
#endif

  DeltaHeadUnit head = {unit.flag, unit.length > head_varint_mask,
                        (uint8_t)(unit.length & head_varint_mask)};
  write_field(buffer, head);

  uint64_t remaining_length = unit.length >> DeltaHeadUnit::lenbits;
  write_varint(buffer, remaining_length);
  if (unit.flag) {
    write_varint(buffer, unit.offset);
  }
}

void GFixSizeChunking(unsigned char* data, int len, int begflag, int begsize,
                      uint32_t* hash_table, int mask) {
  if (len < STRLOOK) return;

  int i = 0;
  int movebitlength = sizeof(FPTYPE) * 8 / STRLOOK;
  if (sizeof(FPTYPE) * 8 % STRLOOK != 0) movebitlength++;
  FPTYPE fingerprint = 0;

  /** GEAR **/
  for (; i < STRLOOK; i++) {
    fingerprint = (fingerprint << (movebitlength)) + GEARmx[data[i]];
  }

  i -= STRLOOK;
  FPTYPE index = 0;
  int numChunks = len - STRLOOK + 1;

  int flag = 0;
  int _begsize = begflag ? begsize : 0;
  while (i < numChunks) {
    if (flag == STRLSTEP) {
      flag = 0;
      index = (fingerprint) >> (sizeof(FPTYPE) * 8 - mask);
      if (hash_table[index] != 0) {
        index = fingerprint >> (sizeof(FPTYPE) * 8 - mask);
      }
      hash_table[index] = i + _begsize;
    }
    /** GEAR **/
    fingerprint = (fingerprint << (movebitlength)) + GEARmx[data[i + STRLOOK]];
    i++;
    flag++;
  }

  return;
}

}  // namespace

int gencode(uint8_t* newBuf, uint32_t newSize, uint8_t* baseBuf,
            uint32_t baseSize, uint8_t** deltaBuf, uint32_t* deltaSize) {
  /* detect the head and tail of one chunk */
  uint32_t beg = 0, end = 0, begSize = 0, endSize = 0;

  if (*deltaBuf == nullptr) {
    *deltaBuf = (uint8_t*)std::malloc(INIT_BUFFER_SIZE);
  }
  uint8_t* databuf = (uint8_t*)std::malloc(INIT_BUFFER_SIZE);
  uint8_t* instbuf = (uint8_t*)std::malloc(INIT_BUFFER_SIZE);

  // Find first difference
  // First in 8 byte blocks and then in 1 byte blocks for speed
  while (begSize + sizeof(uint64_t) <= baseSize &&
         begSize + sizeof(uint64_t) <= newSize &&
         *(uint64_t*)(baseBuf + begSize) == *(uint64_t*)(newBuf + begSize)) {
    begSize += sizeof(uint64_t);
  }

  while (begSize < baseSize && begSize < newSize &&
         baseBuf[begSize] == newBuf[begSize]) {
    begSize++;
  }

  if (begSize > 16)
    beg = 1;
  else
    begSize = 0;

  // Find first difference (from the end)
  while (endSize + sizeof(uint64_t) <= baseSize &&
         endSize + sizeof(uint64_t) <= newSize &&
         *(uint64_t*)(baseBuf + baseSize - endSize - sizeof(uint64_t)) ==
             *(uint64_t*)(newBuf + newSize - endSize - sizeof(uint64_t))) {
    endSize += sizeof(uint64_t);
  }

  while (endSize < baseSize && endSize < newSize &&
         baseBuf[baseSize - endSize - 1] == newBuf[newSize - endSize - 1]) {
    endSize++;
  }

  if (begSize + endSize > newSize) {
    endSize = newSize - begSize;
  }

  if (endSize > 16)
    end = 1;
  else
    endSize = 0;
  /* end of detect */

  BufferStreamDescriptor deltaStream = {*deltaBuf, 0, *deltaSize};
  BufferStreamDescriptor instStream = {instbuf, 0,
                                       sizeof(instbuf)};  // Instruction stream
  BufferStreamDescriptor dataStream = {databuf, 0, sizeof(databuf)};
  BufferStreamDescriptor newStream = {newBuf, begSize, newSize};
  DeltaUnitMem unit = {};  // In-memory represtation of current working unit

  if (begSize + endSize >= baseSize) {  // TODO: test this path
    if (beg) {
      // Data at start is from the original file, write instruction to copy from
      // base
      unit.flag = true;
      unit.offset = 0;
      unit.length = begSize;
      write_unit(instStream, unit);
    }
    if (newSize - begSize - endSize > 0) {
      int32_t litlen = newSize - begSize - endSize;
      unit.flag = false;
      unit.length = litlen;
      write_unit(instStream, unit);
      stream_into(dataStream, newStream, litlen);
    }
    if (end) {
      int32_t matchlen = endSize;
      int32_t offset = baseSize - endSize;
      unit.flag = true;
      unit.offset = offset;
      unit.length = matchlen;
      write_unit(instStream, unit);
    }

    write_varint(deltaStream, instStream.cursor);
    write_concat_buffer(deltaStream, instStream);
    write_concat_buffer(deltaStream, dataStream);

    *deltaSize = deltaStream.cursor;
    *deltaBuf = deltaStream.buf;

    std::free(dataStream.buf);
    std::free(instStream.buf);
    return deltaStream.cursor;
  }

  /* chunk the baseFile */
  int32_t tmp = (baseSize - begSize - endSize) + 10;
  int32_t bit;
  for (bit = 0; tmp; bit++) tmp >>= 1;
  uint64_t xxsize = 0XFFFFFFFFFFFFFFFF >> (64 - bit);  // mask
  uint32_t hash_size = xxsize + 1;
  uint32_t* hash_table = (uint32_t*)std::malloc(hash_size * sizeof(uint32_t));
  std::memset(hash_table, 0, sizeof(uint32_t) * hash_size);

  GFixSizeChunking(baseBuf + begSize, baseSize - begSize - endSize, beg,
                   begSize, hash_table, bit);
  /* end of inserting */

  uint32_t inputPos = begSize;
  uint32_t cursor = 0;
  int32_t movebitlength = 0;
  if (sizeof(FPTYPE) * 8 % STRLOOK == 0)
    movebitlength = sizeof(FPTYPE) * 8 / STRLOOK;
  else
    movebitlength = sizeof(FPTYPE) * 8 / STRLOOK + 1;

  if (beg) {
    // Data at start is from the original file, write instruction to copy from
    // base
    unit.flag = true;
    unit.offset = 0;
    unit.length = begSize;
    write_unit(instStream, unit);
    unit.length = 0;  // Mark as written
  }

  FPTYPE fingerprint = 0;
  for (uint32_t i = 0; i < STRLOOK && i < newSize - endSize - inputPos; i++) {
    fingerprint =
        (fingerprint << (movebitlength)) + GEARmx[(newBuf + inputPos)[i]];
  }

  uint32_t handlebytes = begSize;
  while (inputPos + STRLOOK <= newSize - endSize) {
    uint32_t length;
    bool matchflag = false;
    if (newSize - endSize - inputPos < STRLOOK) {
      cursor = inputPos + (newSize - endSize);
      length = newSize - endSize - inputPos;
    } else {
      cursor = inputPos + STRLOOK;
      length = STRLOOK;
    }
    int32_t index1 = fingerprint >> (sizeof(FPTYPE) * 8 - bit);
    uint32_t offset = 0;
    if (hash_table[index1] != 0 &&
        std::memcmp(newBuf + inputPos, baseBuf + hash_table[index1], length) ==
            0) {
      matchflag = true;
      offset = hash_table[index1];
    }

    /* New data match found in hashtable/base data; attempt to create copy
     * instruction*/
    if (matchflag) {
      // Check how much is possible to copy
      int32_t j = 0;
#if 1 /* 8-bytes optimization */
      while (offset + length + j + 7 < baseSize - endSize &&
             cursor + j + 7 < newSize - endSize &&
             *(uint64_t*)(baseBuf + offset + length + j) ==
                 *(uint64_t*)(newBuf + cursor + j)) {
        j += sizeof(uint64_t);
      }
      while (offset + length + j < baseSize - endSize &&
             cursor + j < newSize - endSize &&
             baseBuf[offset + length + j] == newBuf[cursor + j]) {
        j++;
      }
#endif
      cursor += j;

      int32_t matchlen = cursor - inputPos;
      handlebytes += cursor - inputPos;
      uint64_t _offset = offset;

      // Check if switching modes Literal -> Copy, and dump instruction if
      // available
      if (!unit.flag && unit.length) {
        /* Detect if end of previous literal could have been a partial copy*/
        uint32_t k = 0;
        while (k + 1 <= offset && k + 1 <= unit.length) {
          if (baseBuf[offset - (k + 1)] == newBuf[inputPos - (k + 1)])
            k++;
          else
            break;
        }

        if (k > 0) {
          // Reduce literal by the amount covered by the copy
          unit.length -= k;
          // Set up adjusted copy parameters
          matchlen += k;
          _offset -= k;
          // Last few literal bytes can be overwritten, so move cursor back
          dataStream.cursor -= k;
        }

        write_unit(instStream, unit);
        unit.length = 0;  // Mark written
      }

      unit.flag = true;
      unit.offset = _offset;
      unit.length = matchlen;
      write_unit(instStream, unit);
      unit.length = 0;  // Mark written

      // Update cursor (inputPos) and fingerprint
      for (uint32_t k = cursor;
           k < cursor + STRLOOK && cursor + STRLOOK < newSize - endSize; k++) {
        fingerprint = (fingerprint << (movebitlength)) + GEARmx[newBuf[k]];
      }
      inputPos = cursor;
    } else {  // No match, need to write additional (literal) data
      /*
       * Accumulate length one byte at a time (as literal) in unit while no
       * match is found Pre-emptively write to datastream
       */

      unit.flag = false;
      unit.length += 1;
      stream_from(dataStream, newStream, inputPos, 1);
      handlebytes += 1;

      // Update cursor (inputPos) and fingerprint
      if (inputPos + STRLOOK < newSize - endSize)
        fingerprint = (fingerprint << (movebitlength)) +
                      GEARmx[newBuf[inputPos + STRLOOK]];
      inputPos++;
    }
  }

  // If last unit was unwritten literal, update it to use the rest of the data
  if (!unit.flag && unit.length) {
    newStream.cursor = handlebytes;
    stream_into(dataStream, newStream, newSize - endSize - handlebytes);

    unit.length += (newSize - endSize - handlebytes);
    write_unit(instStream, unit);
    unit.length = 0;
  } else {  // Last unit was Copy, need new instruction
    if (newSize - endSize - handlebytes) {
      newStream.cursor = inputPos;
      stream_into(dataStream, newStream, newSize - endSize - handlebytes);

      unit.flag = false;
      unit.length = newSize - endSize - handlebytes;
      write_unit(instStream, unit);
      unit.length = 0;
    }
  }

  if (end) {
    int32_t matchlen = endSize;
    int32_t offset = baseSize - endSize;

    unit.flag = true;
    unit.offset = offset;
    unit.length = matchlen;
    write_unit(instStream, unit);
    unit.length = 0;
  }

  deltaStream.cursor = 0;
  write_varint(deltaStream, instStream.cursor);
  write_concat_buffer(deltaStream, instStream);
  write_concat_buffer(deltaStream, dataStream);
  *deltaSize = deltaStream.cursor;
  *deltaBuf = deltaStream.buf;

  std::free(dataStream.buf);
  std::free(instStream.buf);
  std::free(hash_table);
  return deltaStream.cursor;
}

int gdecode(uint8_t* deltaBuf, uint32_t deltaSize, uint8_t* baseBuf,
            uint32_t baseSize, uint8_t** outBuf, uint32_t* outSize) {
  if (*outBuf == nullptr) {
    *outBuf = (uint8_t*)std::malloc(INIT_BUFFER_SIZE);
  }

  BufferStreamDescriptor deltaStream = {deltaBuf, 0,
                                        deltaSize};  // Instructions
  const uint64_t instructionLength = read_varint(deltaStream);
  const uint64_t instOffset = deltaStream.cursor;
  BufferStreamDescriptor addDeltaStream = {
      deltaBuf, deltaStream.cursor + instructionLength, deltaSize};
  BufferStreamDescriptor outStream = {*outBuf, 0, *outSize};   // Data out
  BufferStreamDescriptor baseStream = {baseBuf, 0, baseSize};  // Data in
  DeltaUnitMem unit = {};

  while (deltaStream.cursor < instructionLength + instOffset) {
    read_unit(deltaStream, unit);
    if (unit.flag)  // Read from original file using offset
      stream_from(outStream, baseStream, unit.offset, unit.length);
    else  // Read from delta file at current cursor
      stream_into(outStream, addDeltaStream, unit.length);
  }

  *outSize = outStream.cursor;
  *outBuf = outStream.buf;
  return outStream.cursor;
}

int gdecode2(const uint8_t* deltaBuf, const uint32_t deltaSize,
             const uint8_t* baseBuf, const uint32_t baseSize, uint8_t* outBuf,
             const uint32_t outSize) {
  if (outBuf == nullptr) {
    return -1;
  }
  if (outSize == 0) {
    return 0;
  }

  // Data in
  ConstBufferStreamDescriptor baseStream = {baseBuf, 0, baseSize};
  // Instructions
  ConstBufferStreamDescriptor deltaStream = {deltaBuf, 0, deltaSize};
  // Data out
  BufferStreamDescriptor outStream = {outBuf, 0, outSize};
  const uint64_t instructionLength = read_varint(deltaStream);
  const uint64_t instOffset = deltaStream.cursor;
  ConstBufferStreamDescriptor addDeltaStream = {
      deltaBuf, deltaStream.cursor + instructionLength, deltaSize};
  DeltaUnitMem unit = {};

  while (deltaStream.cursor < instructionLength + instOffset) {
    read_unit(deltaStream, unit);
    if (deltaStream.error) {
      return -2;
    }
    if (unit.flag) {  // Read from original file using offset
      if (!stream_from_const(outStream, baseStream, unit.offset, unit.length)) {
        return baseStream.error ? -2 : -1;
      }
    } else {
      // Read from delta file at current cursor
      if (!stream_into_const(outStream, addDeltaStream, unit.length)) {
        return addDeltaStream.error ? -2 : -1;
      }
    }
    if (outStream.error) {
      return -1;
    }
  }

  return outStream.cursor;
}
