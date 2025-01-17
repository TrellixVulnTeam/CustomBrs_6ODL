/*
 * Copyright 2010 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkGr.h"
#include "SkColorFilter.h"
#include "SkConfig8888.h"
#include "SkGpuDevice.h"
#include "SkMessageBus.h"
#include "SkPixelRef.h"
#include "GrResourceCache.h"

/*  Fill out buffer with the compressed format Ganesh expects from a colortable
 based bitmap. [palette (colortable) + indices].

 At the moment Ganesh only supports 8bit version. If Ganesh allowed we others
 we could detect that the colortable.count is <= 16, and then repack the
 indices as nibbles to save RAM, but it would take more time (i.e. a lot
 slower than memcpy), so skipping that for now.

 Ganesh wants a full 256 palette entry, even though Skia's ctable is only as big
 as the colortable.count says it is.
 */
static void build_compressed_data(void* buffer, const SkBitmap& bitmap) {
    SkASSERT(SkBitmap::kIndex8_Config == bitmap.config());

    SkAutoLockPixels alp(bitmap);
    if (!bitmap.readyToDraw()) {
        SkDEBUGFAIL("bitmap not ready to draw!");
        return;
    }

    SkColorTable* ctable = bitmap.getColorTable();
    char* dst = (char*)buffer;

    const int count = ctable->count();

    SkDstPixelInfo dstPI;
    dstPI.fColorType = kRGBA_8888_SkColorType;
    dstPI.fAlphaType = kPremul_SkAlphaType;
    dstPI.fPixels = buffer;
    dstPI.fRowBytes = count * sizeof(SkPMColor);

    SkSrcPixelInfo srcPI;
    srcPI.fColorType = kN32_SkColorType;
    srcPI.fAlphaType = kPremul_SkAlphaType;
    srcPI.fPixels = ctable->lockColors();
    srcPI.fRowBytes = count * sizeof(SkPMColor);

    srcPI.convertPixelsTo(&dstPI, count, 1);

    ctable->unlockColors();

    // always skip a full 256 number of entries, even if we memcpy'd fewer
    dst += kGrColorTableSize;

    if ((unsigned)bitmap.width() == bitmap.rowBytes()) {
        memcpy(dst, bitmap.getPixels(), bitmap.getSize());
    } else {
        // need to trim off the extra bytes per row
        size_t width = bitmap.width();
        size_t rowBytes = bitmap.rowBytes();
        const char* src = (const char*)bitmap.getPixels();
        for (int y = 0; y < bitmap.height(); y++) {
            memcpy(dst, src, width);
            src += rowBytes;
            dst += width;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

static void generate_bitmap_cache_id(const SkBitmap& bitmap, GrCacheID* id) {
    // Our id includes the offset, width, and height so that bitmaps created by extractSubset()
    // are unique.
    uint32_t genID = bitmap.getGenerationID();
    SkIPoint origin = bitmap.pixelRefOrigin();
    int16_t width = SkToS16(bitmap.width());
    int16_t height = SkToS16(bitmap.height());

    GrCacheID::Key key;
    memcpy(key.fData8 +  0, &genID,     4);
    memcpy(key.fData8 +  4, &origin.fX, 4);
    memcpy(key.fData8 +  8, &origin.fY, 4);
    memcpy(key.fData8 + 12, &width,     2);
    memcpy(key.fData8 + 14, &height,    2);
    static const size_t kKeyDataSize = 16;
    memset(key.fData8 + kKeyDataSize, 0, sizeof(key) - kKeyDataSize);
    GR_STATIC_ASSERT(sizeof(key) >= kKeyDataSize);
    static const GrCacheID::Domain gBitmapTextureDomain = GrCacheID::GenerateDomain();
    id->reset(gBitmapTextureDomain, key);
}

static void generate_bitmap_texture_desc(const SkBitmap& bitmap, GrTextureDesc* desc) {
    desc->fFlags = kNone_GrTextureFlags;
    desc->fWidth = bitmap.width();
    desc->fHeight = bitmap.height();
    desc->fConfig = SkImageInfo2GrPixelConfig(bitmap.info());
    desc->fSampleCnt = 0;
}

namespace {

// When the SkPixelRef genID changes, invalidate a corresponding GrResource described by key.
class GrResourceInvalidator : public SkPixelRef::GenIDChangeListener {
public:
    explicit GrResourceInvalidator(GrResourceKey key) : fKey(key) {}
private:
    GrResourceKey fKey;

    virtual void onChange() SK_OVERRIDE {
        const GrResourceInvalidatedMessage message = { fKey };
        SkMessageBus<GrResourceInvalidatedMessage>::Post(message);
    }
};

}  // namespace

static void add_genID_listener(GrResourceKey key, SkPixelRef* pixelRef) {
    SkASSERT(NULL != pixelRef);
    pixelRef->addGenIDChangeListener(SkNEW_ARGS(GrResourceInvalidator, (key)));
}

static GrTexture* sk_gr_create_bitmap_texture(GrContext* ctx,
                                              bool cache,
                                              const GrTextureParams* params,
                                              const SkBitmap& origBitmap) {
    SkBitmap tmpBitmap;

    const SkBitmap* bitmap = &origBitmap;

    GrTextureDesc desc;
    generate_bitmap_texture_desc(*bitmap, &desc);

    if (SkBitmap::kIndex8_Config == bitmap->config()) {
        // build_compressed_data doesn't do npot->pot expansion
        // and paletted textures can't be sub-updated
        if (ctx->supportsIndex8PixelConfig(params, bitmap->width(), bitmap->height())) {
            size_t imagesize = bitmap->width() * bitmap->height() + kGrColorTableSize;
            SkAutoMalloc storage(imagesize);

            build_compressed_data(storage.get(), origBitmap);

            // our compressed data will be trimmed, so pass width() for its
            // "rowBytes", since they are the same now.

            if (cache) {
                GrCacheID cacheID;
                generate_bitmap_cache_id(origBitmap, &cacheID);

                GrResourceKey key;
                GrTexture* result = ctx->createTexture(params, desc, cacheID,
                                                       storage.get(), bitmap->width(), &key);
                if (NULL != result) {
                    add_genID_listener(key, origBitmap.pixelRef());
                }
                return result;
            } else {
                GrTexture* result = ctx->lockAndRefScratchTexture(desc,
                                                            GrContext::kExact_ScratchTexMatch);
                result->writePixels(0, 0, bitmap->width(),
                                    bitmap->height(), desc.fConfig,
                                    storage.get());
                return result;
            }
        } else {
            origBitmap.copyTo(&tmpBitmap, kN32_SkColorType);
            // now bitmap points to our temp, which has been promoted to 32bits
            bitmap = &tmpBitmap;
            desc.fConfig = SkImageInfo2GrPixelConfig(bitmap->info());
        }
    }

    SkAutoLockPixels alp(*bitmap);
    if (!bitmap->readyToDraw()) {
        return NULL;
    }
    if (cache) {
        // This texture is likely to be used again so leave it in the cache
        GrCacheID cacheID;
        generate_bitmap_cache_id(origBitmap, &cacheID);

        GrResourceKey key;
        GrTexture* result = ctx->createTexture(params, desc, cacheID,
                                               bitmap->getPixels(), bitmap->rowBytes(), &key);
        if (NULL != result) {
            add_genID_listener(key, origBitmap.pixelRef());
        }
        return result;
   } else {
        // This texture is unlikely to be used again (in its present form) so
        // just use a scratch texture. This will remove the texture from the
        // cache so no one else can find it. Additionally, once unlocked, the
        // scratch texture will go to the end of the list for purging so will
        // likely be available for this volatile bitmap the next time around.
        GrTexture* result = ctx->lockAndRefScratchTexture(desc, GrContext::kExact_ScratchTexMatch);
        result->writePixels(0, 0,
                            bitmap->width(), bitmap->height(),
                            desc.fConfig,
                            bitmap->getPixels(),
                            bitmap->rowBytes());
        return result;
    }
}

bool GrIsBitmapInCache(const GrContext* ctx,
                       const SkBitmap& bitmap,
                       const GrTextureParams* params) {
    GrCacheID cacheID;
    generate_bitmap_cache_id(bitmap, &cacheID);

    GrTextureDesc desc;
    generate_bitmap_texture_desc(bitmap, &desc);
    return ctx->isTextureInCache(desc, cacheID, params);
}

GrTexture* GrLockAndRefCachedBitmapTexture(GrContext* ctx,
                                           const SkBitmap& bitmap,
                                           const GrTextureParams* params) {
    GrTexture* result = NULL;

    bool cache = !bitmap.isVolatile();

    if (cache) {
        // If the bitmap isn't changing try to find a cached copy first.

        GrCacheID cacheID;
        generate_bitmap_cache_id(bitmap, &cacheID);

        GrTextureDesc desc;
        generate_bitmap_texture_desc(bitmap, &desc);

        result = ctx->findAndRefTexture(desc, cacheID, params);
    }
    if (NULL == result) {
        result = sk_gr_create_bitmap_texture(ctx, cache, params, bitmap);
    }
    if (NULL == result) {
        GrPrintf("---- failed to create texture for cache [%d %d]\n",
                    bitmap.width(), bitmap.height());
    }
    return result;
}

void GrUnlockAndUnrefCachedBitmapTexture(GrTexture* texture) {
    SkASSERT(NULL != texture->getContext());

    texture->getContext()->unlockScratchTexture(texture);
    texture->unref();
}

///////////////////////////////////////////////////////////////////////////////

GrPixelConfig SkBitmapConfig2GrPixelConfig(SkBitmap::Config config) {
    switch (config) {
        case SkBitmap::kA8_Config:
            return kAlpha_8_GrPixelConfig;
        case SkBitmap::kIndex8_Config:
            return kIndex_8_GrPixelConfig;
        case SkBitmap::kRGB_565_Config:
            return kRGB_565_GrPixelConfig;
        case SkBitmap::kARGB_4444_Config:
            return kRGBA_4444_GrPixelConfig;
        case SkBitmap::kARGB_8888_Config:
            return kSkia8888_GrPixelConfig;
        default:
            // kNo_Config, kA1_Config missing
            return kUnknown_GrPixelConfig;
    }
}

// alphatype is ignore for now, but if GrPixelConfig is expanded to encompass
// alpha info, that will be considered.
GrPixelConfig SkImageInfo2GrPixelConfig(SkColorType ct, SkAlphaType) {
    switch (ct) {
        case kUnknown_SkColorType:
            return kUnknown_GrPixelConfig;
        case kAlpha_8_SkColorType:
            return kAlpha_8_GrPixelConfig;
        case kRGB_565_SkColorType:
            return kRGB_565_GrPixelConfig;
        case kARGB_4444_SkColorType:
            return kRGBA_4444_GrPixelConfig;
        case kRGBA_8888_SkColorType:
            return kRGBA_8888_GrPixelConfig;
        case kBGRA_8888_SkColorType:
            return kBGRA_8888_GrPixelConfig;
        case kIndex_8_SkColorType:
            return kIndex_8_GrPixelConfig;
    }
    SkASSERT(0);    // shouldn't get here
    return kUnknown_GrPixelConfig;
}

bool GrPixelConfig2ColorType(GrPixelConfig config, SkColorType* ctOut) {
    SkColorType ct;
    switch (config) {
        case kAlpha_8_GrPixelConfig:
            ct = kAlpha_8_SkColorType;
            break;
        case kIndex_8_GrPixelConfig:
            ct = kIndex_8_SkColorType;
            break;
        case kRGB_565_GrPixelConfig:
            ct = kRGB_565_SkColorType;
            break;
        case kRGBA_4444_GrPixelConfig:
            ct = kARGB_4444_SkColorType;
            break;
        case kRGBA_8888_GrPixelConfig:
            ct = kRGBA_8888_SkColorType;
            break;
        case kBGRA_8888_GrPixelConfig:
            ct = kBGRA_8888_SkColorType;
            break;
        default:
            return false;
    }
    if (ctOut) {
        *ctOut = ct;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////

void SkPaint2GrPaintNoShader(SkGpuDevice* dev, const SkPaint& skPaint, bool justAlpha,
                             bool constantColor, GrPaint* grPaint) {

    grPaint->setDither(skPaint.isDither());
    grPaint->setAntiAlias(skPaint.isAntiAlias());

    SkXfermode::Coeff sm;
    SkXfermode::Coeff dm;

    SkXfermode* mode = skPaint.getXfermode();
    GrEffectRef* xferEffect = NULL;
    if (SkXfermode::AsNewEffectOrCoeff(mode, &xferEffect, &sm, &dm)) {
        if (NULL != xferEffect) {
            grPaint->addColorEffect(xferEffect)->unref();
            sm = SkXfermode::kOne_Coeff;
            dm = SkXfermode::kZero_Coeff;
        }
    } else {
        //SkDEBUGCODE(SkDebugf("Unsupported xfer mode.\n");)
        // Fall back to src-over
        sm = SkXfermode::kOne_Coeff;
        dm = SkXfermode::kISA_Coeff;
    }
    grPaint->setBlendFunc(sk_blend_to_grblend(sm), sk_blend_to_grblend(dm));

    if (justAlpha) {
        uint8_t alpha = skPaint.getAlpha();
        grPaint->setColor(GrColorPackRGBA(alpha, alpha, alpha, alpha));
        // justAlpha is currently set to true only if there is a texture,
        // so constantColor should not also be true.
        SkASSERT(!constantColor);
    } else {
        grPaint->setColor(SkColor2GrColor(skPaint.getColor()));
    }

    SkColorFilter* colorFilter = skPaint.getColorFilter();
    if (NULL != colorFilter) {
        // if the source color is a constant then apply the filter here once rather than per pixel
        // in a shader.
        if (constantColor) {
            SkColor filtered = colorFilter->filterColor(skPaint.getColor());
            grPaint->setColor(SkColor2GrColor(filtered));
        } else {
            SkAutoTUnref<GrEffectRef> effect(colorFilter->asNewEffect(dev->context()));
            if (NULL != effect.get()) {
                grPaint->addColorEffect(effect);
            }
        }
    }
}

void SkPaint2GrPaintShader(SkGpuDevice* dev, const SkPaint& skPaint,
                           bool constantColor, GrPaint* grPaint) {
    SkShader* shader = skPaint.getShader();
    if (NULL == shader) {
        SkPaint2GrPaintNoShader(dev, skPaint, false, constantColor, grPaint);
        return;
    }

    // SkShader::asNewEffect() may do offscreen rendering. Setup default drawing state and require
    // the shader to set a render target.
    GrContext::AutoWideOpenIdentityDraw awo(dev->context(), NULL);

    // setup the shader as the first color effect on the paint
    SkAutoTUnref<GrEffectRef> effect(shader->asNewEffect(dev->context(), skPaint, NULL));
    if (NULL != effect.get()) {
        grPaint->addColorEffect(effect);
        // Now setup the rest of the paint.
        SkPaint2GrPaintNoShader(dev, skPaint, true, false, grPaint);
    } else {
        // We still don't have SkColorShader::asNewEffect() implemented.
        SkShader::GradientInfo info;
        SkColor                color;

        info.fColors = &color;
        info.fColorOffsets = NULL;
        info.fColorCount = 1;
        if (SkShader::kColor_GradientType == shader->asAGradient(&info)) {
            SkPaint copy(skPaint);
            copy.setShader(NULL);
            // modulate the paint alpha by the shader's solid color alpha
            U8CPU newA = SkMulDiv255Round(SkColorGetA(color), copy.getAlpha());
            copy.setColor(SkColorSetA(color, newA));
            SkPaint2GrPaintNoShader(dev, copy, false, constantColor, grPaint);
        } else {
            SkPaint2GrPaintNoShader(dev, skPaint, false, constantColor, grPaint);
        }
    }
}
