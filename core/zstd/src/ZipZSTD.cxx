// Original Author: Brian Bockelman

/*************************************************************************
 * Copyright (C) 1995-2018, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include "ZipZSTD.h"

#include "ROOT/RConfig.hxx"

#include "zdict.h"
#include <zstd.h>
#include <memory>

#include <iostream>

static const int kROOTHeaderSize = 9;

void R__zipZSTD(int cxlevel, int *srcsize, char *src, int *tgtsize, char *tgt, int *irep)
{
    std::cout << "Ziping zstd with R__zipZSTD()->" << std::endl;
    *irep = 0;

    //using Ctx_ptr = std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)>;
    //Ctx_ptr fCtx(ZSTD_createCCtx(), &ZSTD_freeCCtx);
    //ZSTD_CCtx* const cctx = ZSTD_createCCtx();
    //ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, cxlevel);
    //ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1);

    // Initialize imp. values // ->>> ROOT::Internal::ZSTDCompressionEngine zstd(cxlevel);
    int fLevel{cxlevel}; // Compression level

    // ssize_t ZSTDCompressionEngine::StreamFull(const void *buffer, size_t size)
    if (R__unlikely(*tgtsize <= 0)) return;
    if (R__unlikely(*tgtsize < kROOTHeaderSize)) return;
    char *originalLocation = tgt;
    uint64_t in_size = (unsigned)(*srcsize);
    tgt += kROOTHeaderSize;
    *tgtsize -= kROOTHeaderSize;

    //size_t retval = ZSTD_compressCCtx(cctx, tgt, static_cast<size_t>(*tgtsize), src, static_cast<size_t>(*srcsize), fLevel);
    size_t retval = ZSTD_compress(tgt, static_cast<size_t>(*tgtsize), src, static_cast<size_t>(*srcsize), fLevel);
    if (R__unlikely(ZSTD_isError(retval))) return;

    tgt += retval;
    *tgtsize -= retval;

    // WriteROOTHeader(originalLocation, "ZS", Version(), retval, size);
    // Par: void *buffer, const char alg[2], char version, int deflate_size, int inflate_size
    char *tgt_header = static_cast<char *>(originalLocation);
    tgt_header[0] = 'Z';
    tgt_header[1] = 'S';
    tgt_header[2] = '\1';

    tgt_header[3] = static_cast<char>(retval & 0xff);
    tgt_header[4] = static_cast<char>((retval >> 8) & 0xff);
    tgt_header[5] = static_cast<char>((retval >> 16) & 0xff);

    tgt_header[6] = static_cast<char>(in_size & 0xff);
    tgt_header[7] = static_cast<char>((in_size >> 8) & 0xff);
    tgt_header[8] = static_cast<char>((in_size >> 16) & 0xff);
    // Note: return size should include header.
    *irep = (int)retval + kROOTHeaderSize;
    if (*irep < 0) {*irep = 0;}
}

void R__unzipZSTD(int *srcsize, unsigned char *src, int *tgtsize, unsigned char *tgt, int *irep)
{
    std::cout << "Unziping zstd with R__unzipZSTD()->" << std::endl;
    unsigned char *src_unzip = static_cast<unsigned char*>(src);
    *irep = 0;
    if (R__unlikely(src[0] != 'Z' || src[1] != 'S')) {
      fprintf(stderr, "R__unzipZSTD: algorithm run against buffer with incorrect header (got %d%d; expected %d%d).\n",
              src[0], src[1], 'Z', 'S');
      return;
    }

    int ZSTD_version =  ZSTD_versionNumber() / (100 * 100);
    if (R__unlikely(src[2] != ZSTD_version)) {
      fprintf(stderr,
              "R__unzipZSTD: This version of ZSTD is incompatible with the on-disk version (got %d; expected %d).\n",
              src[2], ZSTD_version);
      return;
    }
    src_unzip += kROOTHeaderSize;
    *srcsize -= kROOTHeaderSize;
    unsigned long long const rSize = ZSTD_getFrameContentSize(tgt, *tgtsize);
    //StreamFull(srcsize, src) const void *buffer, size_t size
    size_t retval = ZSTD_decompress((char *)tgt, rSize,
                                   (char *)src_unzip, static_cast<size_t>(*srcsize));
    if (R__unlikely(ZSTD_isError(retval))) return;

    tgt += retval;
    *tgtsize -= retval;

    if (retval < 0) retval = 0;
    *irep = retval;
    printf("Check : %6u -> %7u \n", (unsigned)*src_unzip, (unsigned)rSize);
    return;
}
