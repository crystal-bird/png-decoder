
#include "png.h"

#include <time.h>

#include <stdio.h>
#include <stdlib.h>

internal streaming_buffer ReadEntireFile(char* FileName)
{
    streaming_buffer Result = {0};
    
    FILE* File;
    fopen_s(&File, FileName, "rb");
    if (File)
    {
        fseek(File, 0, SEEK_END);
        Result.ContentsSize = ftell(File);
        fseek(File, 0, SEEK_SET);
        
        Result.Contents = calloc(Result.ContentsSize, sizeof(u8));
        fread(Result.Contents, Result.ContentsSize, 1, File);
        fclose(File);
    }
    
    return (Result);
}

#define Consume(File, Type) (Type*)ConsumeSize(File, sizeof(Type))

internal void* ConsumeSize(streaming_buffer* File, umm Size)
{
    void* Result = 0;
    
    if ((File->ContentsSize == 0) &&
        (File->First))
    {
        streaming_chunk* This = File->First;
        
        File->Contents = This->Contents;
        File->ContentsSize = This->ContentsSize;
        
        File->First = This->Next;
    }
    
    if (File->ContentsSize >= Size)
    {
        Result = File->Contents;
        
        File->Contents = (u8*)File->Contents + Size;
        File->ContentsSize -= Size;
    }
    
    if (!Result)
    {
        printf("File underflow.\n");
    }
    
    return (Result);
}

internal u32 PeekBits(streaming_buffer* File, u32 BitCount)
{
    u32 Result = 0;
    
    while (File->BitCount < BitCount)
    {
        u32 Byte = *Consume(File, u8);
        
        File->BitBuffer |= Byte << File->BitCount;
        File->BitCount += 8;
    }
    
    if (File->BitCount >= BitCount)
    {
        Result = File->BitBuffer & ((1 << BitCount) - 1);
    }
    
    return (Result);
}

internal void DiscardBits(streaming_buffer* File, u32 BitCount)
{
    if (File->BitCount >= BitCount)
    {
        File->BitBuffer >>= BitCount;
        File->BitCount -= BitCount;
    }
}

internal u32 ConsumeBits(streaming_buffer* File, u32 BitCount)
{
    if (BitCount > 32)
    {
        printf("ConsumeBits only supports reading 32 bits at a time.\n");
    }
    
    u32 Result = PeekBits(File, BitCount);
    DiscardBits(File, BitCount);
    
    return (Result);
}

internal void FlushByte(streaming_buffer* File)
{
    File->BitCount = 0;
    File->BitBuffer = 0;
}

internal void EndianSwap32(u32* Value)
{
    *Value = ((*Value >> 24) |
              ((*Value & 0x00FF0000) >> 8) |
              ((*Value << 8) & 0x00FF0000) |
              (*Value << 24));
}

internal u32 ReverseBits(u32 Value, u32 BitCount)
{
    u32 Result = 0;
    
    for (u32 BitIndex = 0;
         BitIndex <= (BitCount/2);
         BitIndex++)
    {
        u32 Inv = BitCount - (BitIndex + 1);
        
        Result |= ((Value >> BitIndex) & 0x1) << Inv;
        Result |= ((Value >> Inv) & 0x1) << BitIndex;
    }
    
    return (Result);
}

internal png_huffman AllocateHuffman(u32 MaxCodeLengthInBits)
{
    png_huffman Huffman = {0};
    
    if (MaxCodeLengthInBits > PNG_MAX_HUFFMAN_BIT_COUNT)
    {
        printf("Code length exceeded max PNG huffman code length.\n");
    }
    
    Huffman.MaxCodeLengthInBits = MaxCodeLengthInBits;
    Huffman.EntryCount = (1 << MaxCodeLengthInBits);
    Huffman.Entries = calloc(Huffman.EntryCount, sizeof(png_huffman_entry));
    
    return (Huffman);
}

internal void ComputeHuffman(umm InputCount, u32* Input, png_huffman* Result)
{
    u32 CodeLengthHist[PNG_MAX_HUFFMAN_BIT_COUNT] = {0};
    for (u32 Index = 0;
         Index < InputCount;
         Index++)
    {
        u32 Symbol = Input[Index];
        ++CodeLengthHist[Symbol];
    }
    
    u32 NextUnusedCode[PNG_MAX_HUFFMAN_BIT_COUNT];
    CodeLengthHist[0] = 0;
    NextUnusedCode[0] = 0;
    for (u32 BitIndex = 1;
         BitIndex < PNG_MAX_HUFFMAN_BIT_COUNT;
         BitIndex++)
    {
        NextUnusedCode[BitIndex] = (NextUnusedCode[BitIndex - 1] +
                                    CodeLengthHist[BitIndex - 1]) << 1;
    }
    
    for (u32 Symbol = 0;
         Symbol < InputCount;
         Symbol++)
    {
        u32 CodeLength = Input[Symbol];
        if (CodeLength)
        {
            u32 Code = NextUnusedCode[CodeLength]++;
            
            u32 ArbitaryBits = Result->MaxCodeLengthInBits - CodeLength;
            u32 EntryCount = (1 << ArbitaryBits);
            
            for (u32 EntryIndex = 0;
                 EntryIndex < EntryCount;
                 EntryIndex++)
            {
                u32 TableIndex = (Code << ArbitaryBits) | EntryIndex;
                TableIndex = ReverseBits(TableIndex, Result->MaxCodeLengthInBits);
                
                png_huffman_entry* Entry = Result->Entries + TableIndex;
                Entry->Symbol = (u16)Symbol;
                Entry->BitsUsed = (u16)CodeLength;
            }
        }
    }
}

internal u32 HuffmanDecode(png_huffman* Huffman, streaming_buffer* Input)
{
    u32 Index = PeekBits(Input, Huffman->MaxCodeLengthInBits);
    
    png_huffman_entry Entry = Huffman->Entries[Index];
    if (Entry.BitsUsed == 0)
    {
        printf("Invalid huffman entry.\n");
    }
    
    u32 Result = Entry.Symbol;
    DiscardBits(Input, Entry.BitsUsed);
    
    return (Result);
}

// NOTE(wetyard): Thanks Sean Barrett from stb_image for these tables.
// I always end up making mistakes while writing them down...
global u32 PNGLengthBase[] =
{
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13,
    15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
    67, 83, 99, 115, 131, 163, 195, 227, 258,
    0, 0,
};

global u32 PNGLengthExtraBits[] =
{
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1,
    2, 2, 2, 2,
    3, 3, 3, 3,
    4, 4, 4, 4,
    5, 5, 5, 5,
    0, 0, 0,
};

global u32 PNGDistBase[] =
{
    1, 2, 3, 4, 5, 7, 9, 13, 
    17, 25, 33, 49, 65, 97, 129, 193, 
    257, 385, 513, 769, 1025, 1537, 2049, 
    3073, 4097, 6145, 8193, 12289, 16385, 24577,
    0, 0, 
};

global u32 PNGDistExtraBits[] =
{
    0, 0, 0, 0,
    1, 1, 2, 2,
    3, 3, 4, 4,
    5, 5, 6, 6,
    7, 7, 8, 8,
    9, 9, 10, 10,
    11, 11, 12, 12,
    13, 13,
};

internal u8 PNGFilter0(u8* X, u32 Channel)
{
    u8 Result = X[Channel];
    return (Result);
}

internal u8 PNGFilter1(u8* X, u8* A, u32 Channel)
{
    u8 Result = X[Channel] + A[Channel];
    return (Result);
}

internal u8 PNGFilter2(u8* X, u8* B, u32 Channel)
{
    u8 Result = X[Channel] + B[Channel];
    return (Result);
}

internal u8 PNGFilter3(u8* X, u8* A, u8* B, u32 Channel)
{
    s32 AV = A[Channel];
    s32 BV = B[Channel];
    
    s32 Average = (AV + BV) / 2;
    
    u8 Result = X[Channel] + (u8)Average;
    return (Result);
}

internal u8 PNGFilter4(u8* X, u8* A, u8* B, u8* C, u32 Channel)
{
    s32 AV = A[Channel];
    s32 BV = B[Channel];
    s32 CV = C[Channel];
    
    s32 P = AV + BV - CV;
    s32 PA = Absolute(P - AV);
    s32 PB = Absolute(P - BV);
    s32 PC = Absolute(P - CV);
    
    u32 Predict = 0;
    if ((PA <= PB) && (PA <= PC))
    {
        Predict = AV;
    }
    else if (PB <= PC)
    {
        Predict = BV;
    }
    else
    {
        Predict = CV;
    }
    
    u32 Result = X[Channel] + Predict;
    return (Result);
}

internal void PNGFilterReconstruct(u8* Dest, u8* Source, u32 Width, u32 Height, u32 Channels)
{
    // TODO(wetyard): Lots of opportunities for SIMD here...
    
    u32 Zero = 0;
    u8* PriorRow = (u8*)&Zero;
    u32 PriorRowAdvance = 0;
    
    for (u32 Y = 0; Y < Height; Y++)
    {
        u8 Filter = *Source++;
        u8* CurrentRow = Dest;
        
        switch (Filter)
        {
            case 0:
            {
                for (u32 X = 0; X < Width; X++)
                {
                    for (u32 Index = 0; Index < Channels; Index++)
                    {
                        CurrentRow[Index] = PNGFilter0(Source, Index);
                    }
                    
                    CurrentRow += Channels;
                    Source += Channels;
                }
            } break;
            
            case 1:
            {
                u8* APixel = (u8*)&Zero;
                for (u32 X = 0; X < Width; X++)
                {
                    for (u32 Index = 0; Index < Channels; Index++)
                    {
                        CurrentRow[Index] = PNGFilter1(Source, APixel, Index);
                    }
                    
                    APixel = CurrentRow;
                    
                    CurrentRow += Channels;
                    Source += Channels;
                }
            } break;
            
            case 2:
            {
                u8* BPixel = PriorRow;
                for (u32 X = 0; X < Width; X++)
                {
                    for (u32 Index = 0; Index < Channels; Index++)
                    {
                        CurrentRow[Index] = PNGFilter2(Source, BPixel, Index);
                    }
                    
                    BPixel += PriorRowAdvance;
                    
                    CurrentRow += Channels;
                    Source += Channels;
                }
            } break;
            
            case 3:
            {
                u8* APixel = (u8*)&Zero;
                u8* BPixel = PriorRow;
                for (u32 X = 0; X < Width; X++)
                {
                    for (u32 Index = 0; Index < Channels; Index++)
                    {
                        CurrentRow[Index] = PNGFilter3(Source, APixel, BPixel, Index);
                    }
                    
                    APixel = CurrentRow;
                    BPixel += PriorRowAdvance;
                    
                    CurrentRow += Channels;
                    Source += Channels;
                }
            } break;
            
            case 4:
            {
                u8* APixel = (u8*)&Zero;
                u8* BPixel = PriorRow;
                u8* CPixel = (u8*)&Zero;
                for (u32 X = 0; X < Width; X++)
                {
                    for (u32 Index = 0; Index < Channels; Index++)
                    {
                        CurrentRow[Index] = PNGFilter4(Source, APixel, BPixel, CPixel, Index);
                    }
                    
                    APixel = CurrentRow;
                    CPixel = BPixel;
                    BPixel += PriorRowAdvance;
                    
                    CurrentRow += Channels;
                    Source += Channels;
                }
            } break;
        }
        
        PriorRow = Dest;
        PriorRowAdvance = Channels;
        
        Dest += Width*Channels;
    }
}

internal image ParsePNG(streaming_buffer PNGFile)
{
    streaming_buffer* At = &PNGFile;
    
    u64 FileSignature = *Consume(At, u64);
    if (FileSignature != 0x0A1A0A0D474E5089)
    {
        printf("File signature mismatch.\n");
    }
    
    u32 Width = 0;
    u32 Height = 0;
    u32 Channels = 0;
    
    b32 Supported = false;
    streaming_buffer CompData = {0};
    
    umm ParseStart = clock();
    
    while (At->ContentsSize)
    {
        png_chunk_header* ChunkHeader = Consume(At, png_chunk_header);
        EndianSwap32(&ChunkHeader->Length);
        
        void* ChunkData = ConsumeSize(At, ChunkHeader->Length);
        if (ChunkHeader->TypeU32 == FourCC("IHDR"))
        {
            png_ihdr_chunk* IHDR = (png_ihdr_chunk*)ChunkData;
            
            EndianSwap32(&IHDR->Width);
            EndianSwap32(&IHDR->Height);
            
            Width = IHDR->Width;
            Height = IHDR->Height;
            
            if (IHDR->ColorType == 2)
            {
                Channels = 3;
            }
            else if (IHDR->ColorType == 6)
            {
                Channels = 4;
            }
            
            if ((IHDR->BitDepth == 8) &&
                (IHDR->CompressionMethod == 0) &&
                (IHDR->FilterMethod == 0) &&
                (IHDR->InterlaceMethod == 0))
            {
                Supported = true;
            }
        }
        else if (ChunkHeader->TypeU32 == FourCC("IDAT"))
        {
            streaming_chunk* Chunk = malloc(sizeof(streaming_chunk));
            
            Chunk->Contents = ChunkData;
            Chunk->ContentsSize = ChunkHeader->Length;
            
            if (CompData.Last)
            {
                CompData.Last->Next = Chunk;
            }
            else
            {
                CompData.First = Chunk;
            }
            
            CompData.Last = Chunk;
        }
        
        png_chunk_footer* ChunkFooter = Consume(At, png_chunk_footer);
        EndianSwap32(&ChunkFooter->CheckValue);
    }
    
    umm ParseEnd = clock();
    printf("Parsing PNG chunks took: %.2fms\n", 1000.0 * (ParseEnd - ParseStart) / CLOCKS_PER_SEC);
    
    printf("Width: %u\n", Width);
    printf("Height: %u\n", Height);
    printf("Channels: %u\n", Channels);
    
    u8* FinalPixels = 0;
    
    umm DecompressStart = clock();
    if (Supported)
    {
        png_idat_header* IDATHeader = Consume(&CompData, png_idat_header);
        
        u8 CM = IDATHeader->ZLibMethodFlags & 0xF;
        u8 CINFO = (IDATHeader->ZLibMethodFlags >> 4) & 0xF;
        u8 FCHECK = IDATHeader->AdditionalFlags & 0x1F;
        u8 FDICT = (IDATHeader->AdditionalFlags >> 5) & 0x1;
        u8 FLEVEL = (IDATHeader->AdditionalFlags >> 6) & 0x3;
        
        Supported = ((CM == 8) && (CINFO == 7));
        
        if (Supported)
        {
            printf("Decompressing...\n");
            
            u8* DecompressedPixels = calloc(Width * Height * Channels, sizeof(u8));
            u8* Dest = DecompressedPixels;
            
            png_huffman DictHuffman = AllocateHuffman(7);
            png_huffman LitLenHuffman = AllocateHuffman(15);
            png_huffman DistHuffman = AllocateHuffman(15);
            
            u32 BFINAL = 0;
            while (BFINAL == 0)
            {
                BFINAL = ConsumeBits(&CompData, 1);
                u32 BTYPE = ConsumeBits(&CompData, 2);
                
                if (BTYPE == 0)
                {
                    FlushByte(&CompData);
                    
                    u16 LEN = *Consume(&CompData, u16);
                    u16 NLEN = *Consume(&CompData, u16);
                    
                    if ((u16)(LEN) != (u16)(~NLEN))
                    {
                        printf("LEN/NLEN mismatch.\n");
                    }
                    
                    u8* Source = ConsumeSize(&CompData, LEN);
                    while (LEN--)
                    {
                        *Dest++ = *Source++;
                    }
                }
                else if (BTYPE == 3)
                {
                    printf("BTYPE of %u encountered.\n", BTYPE);
                }
                else
                {
                    u32 LitLenDistTable[512] = {0};
                    
                    u32 HLIT = 0;
                    u32 HDIST = 0;
                    
                    if (BTYPE == 2)
                    {
                        HLIT = ConsumeBits(&CompData, 5);
                        HDIST = ConsumeBits(&CompData, 5);
                        u32 HCLEN = ConsumeBits(&CompData, 4);
                        
                        HLIT += 257;
                        HDIST += 1;
                        HCLEN += 4;
                        
                        u32 HCLENSwizzle[] =
                        {
                            16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
                        };
                        
                        u32 HCLENTable[ArrayCount(HCLENSwizzle)] = {0};
                        for (u32 Index = 0;
                             Index < HCLEN;
                             Index++)
                        {
                            HCLENTable[HCLENSwizzle[Index]] = ConsumeBits(&CompData, 3);
                        }
                        
                        ComputeHuffman(ArrayCount(HCLENSwizzle), HCLENTable, &DictHuffman);
                        
                        u32 LitLenCount = 0;
                        u32 LenCount = HLIT + HDIST;
                        while (LitLenCount != LenCount)
                        {
                            u32 EncodedLen = HuffmanDecode(&DictHuffman, &CompData);
                            
                            u32 RepeatCount = 1;
                            u32 RepeatValue = 0;
                            
                            if (EncodedLen <= 15)
                            {
                                RepeatValue = EncodedLen;
                            }
                            else if (EncodedLen == 16)
                            {
                                RepeatCount = 3 + ConsumeBits(&CompData, 2);
                                RepeatValue = LitLenDistTable[LitLenCount - 1];
                            }
                            else if (EncodedLen == 17)
                            {
                                RepeatCount = 3 + ConsumeBits(&CompData, 3);
                            }
                            else if (EncodedLen == 18)
                            {
                                RepeatCount = 11 + ConsumeBits(&CompData, 7);
                            }
                            else
                            {
                                printf("EncodedLen of %u encountered.\n", EncodedLen);
                            }
                            
                            while (RepeatCount--)
                            {
                                LitLenDistTable[LitLenCount++] = RepeatValue;
                            }
                        }
                    }
                    else if (BTYPE == 1)
                    {
                        HLIT = 288;
                        HDIST = 32;
                        
                        u32 BitCounts[][2] =
                        {
                            {143, 8},
                            {255, 9},
                            {279, 7},
                            {287, 8},
                            {319, 5},
                        };
                        
                        u32 BitCountIndex = 0;
                        for (u32 RangeIndex = 0;
                             RangeIndex < ArrayCount(BitCounts);
                             RangeIndex++)
                        {
                            u32 BitCount = BitCounts[RangeIndex][1];
                            u32 LastValue = BitCounts[RangeIndex][0];
                            while (BitCountIndex <= LastValue)
                            {
                                LitLenDistTable[BitCountIndex++] = BitCount;
                            }
                        }
                    }
                    
                    ComputeHuffman(HLIT, LitLenDistTable, &LitLenHuffman);
                    ComputeHuffman(HDIST, LitLenDistTable + HLIT, &DistHuffman);
                    
                    for (;;)
                    {
                        u32 LitLen = HuffmanDecode(&LitLenHuffman, &CompData);
                        
                        if (LitLen <= 255)
                        {
                            *Dest++ = (LitLen & 0xFF);
                        }
                        else if (LitLen >= 257)
                        {
                            u32 LenCode = LitLen - 257;
                            u32 Length = PNGLengthBase[LenCode];
                            u32 LengthExtraBits = PNGLengthExtraBits[LenCode];
                            if (LengthExtraBits)
                            {
                                Length += ConsumeBits(&CompData, LengthExtraBits);
                            }
                            
                            u32 DistCode = HuffmanDecode(&DistHuffman, &CompData);
                            u32 Dist = PNGDistBase[DistCode];
                            u32 DistExtraBits = PNGDistExtraBits[DistCode];
                            if (DistExtraBits)
                            {
                                Dist += ConsumeBits(&CompData, DistExtraBits);
                            }
                            
                            u8* Source = Dest - Dist;
                            while (Length--)
                            {
                                *Dest++ = *Source++;
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                }
            }
            
            FinalPixels = calloc(Width * Height * Channels, sizeof(u8));
            PNGFilterReconstruct(FinalPixels, DecompressedPixels, Width, Height, Channels);
        }
    }
    umm DecompressEnd = clock();
    printf("Decompression and reconstruction took: %.2fms\n", 1000.0 * (DecompressEnd - DecompressStart) / CLOCKS_PER_SEC);
    
    image Result = {0};
    Result.Data = FinalPixels;
    Result.Width = Width;
    Result.Height = Height;
    Result.Channels = Channels;
    
    return (Result);
}

s32 main(s32 ArgCount, char* Args[])
{
    setvbuf(stdout, 0, _IONBF, 0);
    
    if (ArgCount != 2)
    {
        printf("Usage: %s <PNG file>\n", Args[0]);
    }
    else
    {
        char* FileName = Args[1];
        
        streaming_buffer PNGFile = ReadEntireFile(FileName);
        if (PNGFile.Contents)
        {
            image Image = ParsePNG(PNGFile);
            printf("Hooray!\n");
        }
    }
    
    return (0);
}
