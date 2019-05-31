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

static const int kHeaderSize = 9;

void R__zipZSTD(int cxlevel, int *srcsize, char *src, int *tgtsize, char *tgt, int *irep)
{
    std::cout << "zip zstd" << std::endl;
    using Ctx_ptr = std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)>;
    Ctx_ptr fCtx{ZSTD_createCCtx(), &ZSTD_freeCCtx};

    *irep = 0;
    if (R__unlikely(*tgtsize < kHeaderSize)) {
        std::cout << "Error: target buffer too small in ZSTD" << std::endl;
        return;
    }
    char *originalLocation = tgt;
    tgt += kHeaderSize;
    *tgtsize -= kHeaderSize;


    //StreamFull(srcsize, src) const void *buffer, size_t size
    size_t retval = ZSTD_compressCCtx(  fCtx.get(),
                                        tgt, static_cast<size_t>(*tgtsize),
                                        src, static_cast<size_t>(*srcsize),
                                        2*cxlevel);

    if (R__unlikely(ZSTD_isError(retval))) {
        std::cout << "Error in ZSTD" << std::endl;
        return;
    }
    else {
        tgt += retval;
        *tgtsize -= retval;
        *irep = static_cast<size_t>(retval + kHeaderSize);
    }

    char deflate_size = retval;
    char inflate_size = static_cast<size_t>(*srcsize);
    originalLocation[0] = 'Z';
    originalLocation[1] = 'S';
    originalLocation[2] = '\1'; ///?
    originalLocation[3] = deflate_size & 0xff;
    originalLocation[4] = (deflate_size >> 8) & 0xff;
    originalLocation[5] = (deflate_size >> 16) & 0xff;
    originalLocation[6] = inflate_size & 0xff;
    originalLocation[7] = (inflate_size >> 8) & 0xff;
    originalLocation[8] = (inflate_size >> 16) & 0xff;

    return;
}

void R__unzipZSTD(int *srcsize, unsigned char *src, int *tgtsize, unsigned char *tgt, int *irep)
{
    std::cout << "Unzip zstd" << std::endl;
    using Ctx_ptr = std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)>;
    Ctx_ptr fCtx{ZSTD_createDCtx(), &ZSTD_freeDCtx};

    src += kHeaderSize;
    *srcsize -= kHeaderSize;

    //StreamFull(srcsize, src) const void *buffer, size_t size
    size_t retval = ZSTD_decompressDCtx(fCtx.get(),
                                        (char *)tgt, static_cast<size_t>(*tgtsize),
                                        (char *)src, static_cast<size_t>(*srcsize));
    if (R__unlikely(ZSTD_isError(retval))) {
      *irep = 0;
    }
    else {
        tgt += retval;
        *tgtsize -= retval;
        *irep = static_cast<size_t>(retval);
    }

    return;
}
