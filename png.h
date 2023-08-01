/* date = August 1st 2023 10:30 am */

#ifndef PNG_H
#define PNG_H

#define global static
#define persist static
#define internal static

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))

#define Absolute(Value) ((Value) < 0 ? -(Value) : (Value))

typedef signed char s8;
typedef signed short s16;
typedef signed int s32;
typedef signed long long s64;

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef s64 smm;
typedef u64 umm;

typedef float f32;
typedef double f64;

typedef u8 b8;
typedef u32 b32;

#define true 1
#define false 0

#define FourCC(String) \
(((u32)String[0] << 0 ) | \
((u32)String[1] << 8 ) | \
((u32)String[2] << 16) | \
((u32)String[3] << 24))

#pragma pack(push, 1)
typedef struct
{
    u32 Length;
    union
    {
        u32 TypeU32;
        u8 Type[4];
    };
} png_chunk_header;

typedef struct
{
    u32 CheckValue;
} png_chunk_footer;

typedef struct
{
    u32 Width;
    u32 Height;
    u8 BitDepth;
    u8 ColorType;
    u8 CompressionMethod;
    u8 FilterMethod;
    u8 InterlaceMethod;
} png_ihdr_chunk;

typedef struct
{
    u8 ZLibMethodFlags;
    u8 AdditionalFlags;
} png_idat_header;
#pragma pack(pop)

typedef struct streaming_chunk
{
    void* Contents;
    umm ContentsSize;
    
    struct streaming_chunk* Next;
} streaming_chunk;

typedef struct
{
    void* Contents;
    umm ContentsSize;
    
    u32 BitCount;
    u32 BitBuffer;
    
    streaming_chunk* First;
    streaming_chunk* Last;
} streaming_buffer;

typedef struct
{
    u16 Symbol;
    u16 BitsUsed;
} png_huffman_entry;

#define PNG_MAX_HUFFMAN_BIT_COUNT 16
typedef struct
{
    u32 MaxCodeLengthInBits;
    u32 EntryCount;
    png_huffman_entry* Entries;
} png_huffman;

typedef struct
{
    u8* Data;
    u32 Width;
    u32 Height;
    u32 Channels;
} image;

#endif //PNG_H
