#include <contrib/libs/zstd06/renames.h>
/*
    zstd_v05 - decoder for 0.5 format
    Header File
    Copyright (C) 2014-2016, Yann Collet.

    BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the
    distribution.
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - zstd source repository : https://github.com/Cyan4973/zstd
*/
#ifndef ZSTDv05_H
#define ZSTDv05_H

#if defined (__cplusplus)
extern "C" {
#endif

/*-*************************************
*  Dependencies
***************************************/
#include <stddef.h>   /* size_t */
#include "mem.h"      /* U64, U32 */


/* *************************************
*  Simple functions
***************************************/
/*! ZSTDv05_decompress() :
    `compressedSize` : is the _exact_ size of the compressed blob, otherwise decompression will fail.
    `dstCapacity` must be large enough, equal or larger than originalSize.
    @return : the number of bytes decompressed into `dst` (<= `dstCapacity`),
              or an errorCode if it fails (which can be tested using ZSTDv05_isError()) */
size_t ZSTDv05_decompress( void* dst, size_t dstCapacity,
                     const void* src, size_t compressedSize);


/* *************************************
*  Helper functions
***************************************/
/* Error Management */
unsigned    ZSTDv05_isError(size_t code);          /*!< tells if a `size_t` function result is an error code */
const char* ZSTDv05_getErrorName(size_t code);     /*!< provides readable string for an error code */


/* *************************************
*  Explicit memory management
***************************************/
/** Decompression context */
typedef struct ZSTDv05_DCtx_s ZSTDv05_DCtx;
ZSTDv05_DCtx* ZSTDv05_createDCtx(void);
size_t ZSTDv05_freeDCtx(ZSTDv05_DCtx* dctx);      /*!< @return : errorCode */

/** ZSTDv05_decompressDCtx() :
*   Same as ZSTDv05_decompress(), but requires an already allocated ZSTDv05_DCtx (see ZSTDv05_createDCtx()) */
size_t ZSTDv05_decompressDCtx(ZSTDv05_DCtx* ctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize);


/*-***********************
*  Simple Dictionary API
*************************/
/*! ZSTDv05_decompress_usingDict() :
*   Decompression using a pre-defined Dictionary content (see dictBuilder).
*   Dictionary must be identical to the one used during compression, otherwise regenerated data will be corrupted.
*   Note : dict can be NULL, in which case, it's equivalent to ZSTDv05_decompressDCtx() */
size_t ZSTDv05_decompress_usingDict(ZSTDv05_DCtx* dctx,
                                            void* dst, size_t dstCapacity,
                                      const void* src, size_t srcSize,
                                      const void* dict,size_t dictSize);

/*-************************
*  Advanced Streaming API
***************************/
typedef enum { ZSTDv05_fast, ZSTDv05_greedy, ZSTDv05_lazy, ZSTDv05_lazy2, ZSTDv05_btlazy2, ZSTDv05_opt, ZSTDv05_btopt } ZSTDv05_strategy;
typedef struct {
    U64 srcSize;
    U32 windowLog;     /* the only useful information to retrieve */
    U32 contentLog; U32 hashLog; U32 searchLog; U32 searchLength; U32 targetLength; ZSTDv05_strategy strategy;
} ZSTDv05_parameters;
size_t ZSTDv05_getFrameParams(ZSTDv05_parameters* params, const void* src, size_t srcSize);

size_t ZSTDv05_decompressBegin_usingDict(ZSTDv05_DCtx* dctx, const void* dict, size_t dictSize);
void   ZSTDv05_copyDCtx(ZSTDv05_DCtx* dstDCtx, const ZSTDv05_DCtx* srcDCtx);
size_t ZSTDv05_nextSrcSizeToDecompress(ZSTDv05_DCtx* dctx);
size_t ZSTDv05_decompressContinue(ZSTDv05_DCtx* dctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize);


/*-***********************
*  ZBUFF API
*************************/
typedef struct ZBUFFv05_DCtx_s ZBUFFv05_DCtx;
ZBUFFv05_DCtx* ZBUFFv05_createDCtx(void);
size_t         ZBUFFv05_freeDCtx(ZBUFFv05_DCtx* dctx);

size_t ZBUFFv05_decompressInit(ZBUFFv05_DCtx* dctx);
size_t ZBUFFv05_decompressInitDictionary(ZBUFFv05_DCtx* dctx, const void* dict, size_t dictSize);

size_t ZBUFFv05_decompressContinue(ZBUFFv05_DCtx* dctx,
                                            void* dst, size_t* dstCapacityPtr,
                                      const void* src, size_t* srcSizePtr);

/*-***************************************************************************
*  Streaming decompression
*
*  A ZBUFFv05_DCtx object is required to track streaming operations.
*  Use ZBUFFv05_createDCtx() and ZBUFFv05_freeDCtx() to create/release resources.
*  Use ZBUFFv05_decompressInit() to start a new decompression operation,
*   or ZBUFFv05_decompressInitDictionary() if decompression requires a dictionary.
*  Note that ZBUFFv05_DCtx objects can be reused multiple times.
*
*  Use ZBUFFv05_decompressContinue() repetitively to consume your input.
*  *srcSizePtr and *dstCapacityPtr can be any size.
*  The function will report how many bytes were read or written by modifying *srcSizePtr and *dstCapacityPtr.
*  Note that it may not consume the entire input, in which case it's up to the caller to present remaining input again.
*  The content of @dst will be overwritten (up to *dstCapacityPtr) at each function call, so save its content if it matters or change @dst.
*  @return : a hint to preferred nb of bytes to use as input for next function call (it's only a hint, to help latency)
*            or 0 when a frame is completely decoded
*            or an error code, which can be tested using ZBUFFv05_isError().
*
*  Hint : recommended buffer sizes (not compulsory) : ZBUFFv05_recommendedDInSize() / ZBUFFv05_recommendedDOutSize()
*  output : ZBUFFv05_recommendedDOutSize==128 KB block size is the internal unit, it ensures it's always possible to write a full block when decoded.
*  input  : ZBUFFv05_recommendedDInSize==128Kb+3; just follow indications from ZBUFFv05_decompressContinue() to minimize latency. It should always be <= 128 KB + 3 .
* *******************************************************************************/


/* *************************************
*  Tool functions
***************************************/
unsigned ZBUFFv05_isError(size_t errorCode);
const char* ZBUFFv05_getErrorName(size_t errorCode);

/** Functions below provide recommended buffer sizes for Compression or Decompression operations.
*   These sizes are just hints, and tend to offer better latency */
size_t ZBUFFv05_recommendedDInSize(void);
size_t ZBUFFv05_recommendedDOutSize(void);



/*-*************************************
*  Constants
***************************************/
#define ZSTDv05_MAGICNUMBER 0xFD2FB525   /* v0.5 */




#if defined (__cplusplus)
}
#endif

#endif  /* ZSTDv0505_H */
