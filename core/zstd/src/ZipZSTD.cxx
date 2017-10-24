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

void R__zipZSTD(int cxlevel, int *srcsize, char *src, int *tgtsize, char *tgt, int *irep)
{
    //
    using Ctx_ptr = std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)>;
    Ctx_ptr fCtx{nullptr, &ZSTD_freeCCtx};

    //Constructor(cxlevel)
    //int fLevel = cxlevel;

    //SetTarget(tgt, tgtsize) (void *buffer, size_t size)
    char * fBuffer = tgt;
    char * fCur = tgt;
    size_t fSize = static_cast<size_t>(*tgtsize);
    size_t fCap = static_cast<size_t>(*tgtsize);

    //StreamFull(srcsize, src) const void *buffer, size_t size
    size_t retval = ZSTD_compressCCtx(fCtx.get(), fCur, fCap, srcsize, static_cast<size_t>(*src), 2*cxlevel);
    if (R__unlikely(ZSTD_isError(retval))) {
      *irep = -1;
    }
    else {
        fCur += retval;
        fCap -= retval;
        *irep = static_cast<size_t>(retval);
    }

    return;
}

void R__unzipZSTD(int *srcsize, unsigned char *src, int *tgtsize, unsigned char *tgt, int *irep)
{
    //
    using Ctx_ptr = std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)>;
    Ctx_ptr fCtx{nullptr, &ZSTD_freeCCtx};


    //SetTarget(tgt, tgtsize) (void *buffer, size_t size)
    char * fBuffer = tgt;
    char * fCur = tgt;
    size_t fSize = static_cast<size_t>(*tgtsize);
    size_t fCap = static_cast<size_t>(*tgtsize);

    //StreamFull(srcsize, src) const void *buffer, size_t size
    size_t retval = ZSTD_decompressCCtx(fCtx.get(), fCur, fCap, srcsize, static_cast<size_t>(*src));
    if (R__unlikely(ZSTD_isError(retval))) {
      *irep = -1;
    }
    else {
        fCur += retval;
        fCap -= retval;
        *irep = static_cast<size_t>(retval);
    }

    return;
}
