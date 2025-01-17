/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkTypes.h"
#undef GetGlyphIndices

#include "SkAdvancedTypefaceMetrics.h"
#include "SkColorFilter.h"
#include "SkDWrite.h"
#include "SkDWriteFontFileStream.h"
#include "SkDWriteGeometrySink.h"
#include "SkDescriptor.h"
#include "SkEndian.h"
#include "SkFontDescriptor.h"
#include "SkFontHost.h"
#include "SkFontMgr.h"
#include "SkFontStream.h"
#include "SkGlyph.h"
#include "SkHRESULT.h"
#include "SkMaskGamma.h"
#include "SkMatrix22.h"
#include "SkOnce.h"
#include "SkOTTable_EBLC.h"
#include "SkOTTable_EBSC.h"
#include "SkOTTable_head.h"
#include "SkOTTable_hhea.h"
#include "SkOTTable_OS_2.h"
#include "SkOTTable_post.h"
#include "SkPath.h"
#include "SkStream.h"
#include "SkString.h"
#include "SkTScopedComPtr.h"
#include "SkThread.h"
#include "SkTypeface_win.h"
#include "SkTypefaceCache.h"
#include "SkUtils.h"

#include <dwrite.h>

static bool isLCD(const SkScalerContext::Rec& rec) {
    return SkMask::kLCD16_Format == rec.fMaskFormat ||
           SkMask::kLCD32_Format == rec.fMaskFormat;
}

///////////////////////////////////////////////////////////////////////////////

class StreamFontFileLoader;

class SkFontMgr_DirectWrite : public SkFontMgr {
public:
    /** localeNameLength must include the null terminator. */
    SkFontMgr_DirectWrite(IDWriteFactory* factory, IDWriteFontCollection* fontCollection,
                          WCHAR* localeName, int localeNameLength)
        : fFactory(SkRefComPtr(factory))
        , fFontCollection(SkRefComPtr(fontCollection))
        , fLocaleName(localeNameLength)
    {
        memcpy(fLocaleName.get(), localeName, localeNameLength * sizeof(WCHAR));
    }

    /** Creates a typeface using a typeface cache. */
    SkTypeface* createTypefaceFromDWriteFont(IDWriteFontFace* fontFace,
                                             IDWriteFont* font,
                                             IDWriteFontFamily* fontFamily) const;

protected:
    virtual int onCountFamilies() const SK_OVERRIDE;
    virtual void onGetFamilyName(int index, SkString* familyName) const SK_OVERRIDE;
    virtual SkFontStyleSet* onCreateStyleSet(int index) const SK_OVERRIDE;
    virtual SkFontStyleSet* onMatchFamily(const char familyName[]) const SK_OVERRIDE;
    virtual SkTypeface* onMatchFamilyStyle(const char familyName[],
                                           const SkFontStyle& fontstyle) const SK_OVERRIDE;
    virtual SkTypeface* onMatchFaceStyle(const SkTypeface* familyMember,
                                         const SkFontStyle& fontstyle) const SK_OVERRIDE;
    virtual SkTypeface* onCreateFromStream(SkStream* stream, int ttcIndex) const SK_OVERRIDE;
    virtual SkTypeface* onCreateFromData(SkData* data, int ttcIndex) const SK_OVERRIDE;
    virtual SkTypeface* onCreateFromFile(const char path[], int ttcIndex) const SK_OVERRIDE;
    virtual SkTypeface* onLegacyCreateTypeface(const char familyName[],
                                               unsigned styleBits) const SK_OVERRIDE;

private:
    HRESULT getByFamilyName(const WCHAR familyName[], IDWriteFontFamily** fontFamily) const;
    HRESULT getDefaultFontFamily(IDWriteFontFamily** fontFamily) const;

    void Add(SkTypeface* face, SkTypeface::Style requestedStyle, bool strong) const {
        SkAutoMutexAcquire ama(fTFCacheMutex);
        fTFCache.add(face, requestedStyle, strong);
    }

    SkTypeface* FindByProcAndRef(SkTypefaceCache::FindProc proc, void* ctx) const {
        SkAutoMutexAcquire ama(fTFCacheMutex);
        SkTypeface* typeface = fTFCache.findByProcAndRef(proc, ctx);
        return typeface;
    }

    SkTScopedComPtr<IDWriteFactory> fFactory;
    SkTScopedComPtr<IDWriteFontCollection> fFontCollection;
    SkSMallocWCHAR fLocaleName;
    mutable SkMutex fTFCacheMutex;
    mutable SkTypefaceCache fTFCache;

    friend class SkFontStyleSet_DirectWrite;
};

class SkFontStyleSet_DirectWrite : public SkFontStyleSet {
public:
    SkFontStyleSet_DirectWrite(const SkFontMgr_DirectWrite* fontMgr,
                               IDWriteFontFamily* fontFamily)
        : fFontMgr(SkRef(fontMgr))
        , fFontFamily(SkRefComPtr(fontFamily))
    { }

    virtual int count() SK_OVERRIDE;
    virtual void getStyle(int index, SkFontStyle* fs, SkString* styleName) SK_OVERRIDE;
    virtual SkTypeface* createTypeface(int index) SK_OVERRIDE;
    virtual SkTypeface* matchStyle(const SkFontStyle& pattern) SK_OVERRIDE;

private:
    SkAutoTUnref<const SkFontMgr_DirectWrite> fFontMgr;
    SkTScopedComPtr<IDWriteFontFamily> fFontFamily;
};

///////////////////////////////////////////////////////////////////////////////

class StreamFontFileLoader : public IDWriteFontFileLoader {
public:
    // IUnknown methods
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject);
    virtual ULONG STDMETHODCALLTYPE AddRef();
    virtual ULONG STDMETHODCALLTYPE Release();

    // IDWriteFontFileLoader methods
    virtual HRESULT STDMETHODCALLTYPE CreateStreamFromKey(
        void const* fontFileReferenceKey,
        UINT32 fontFileReferenceKeySize,
        IDWriteFontFileStream** fontFileStream);

    static HRESULT Create(SkStream* stream, StreamFontFileLoader** streamFontFileLoader) {
        *streamFontFileLoader = new StreamFontFileLoader(stream);
        if (NULL == streamFontFileLoader) {
            return E_OUTOFMEMORY;
        }
        return S_OK;
    }

    SkAutoTUnref<SkStream> fStream;

private:
    StreamFontFileLoader(SkStream* stream) : fRefCount(1), fStream(SkRef(stream)) { }

    ULONG fRefCount;
};

HRESULT StreamFontFileLoader::QueryInterface(REFIID iid, void** ppvObject) {
    if (iid == IID_IUnknown || iid == __uuidof(IDWriteFontFileLoader)) {
        *ppvObject = this;
        AddRef();
        return S_OK;
    } else {
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }
}

ULONG StreamFontFileLoader::AddRef() {
    return InterlockedIncrement(&fRefCount);
}

ULONG StreamFontFileLoader::Release() {
    ULONG newCount = InterlockedDecrement(&fRefCount);
    if (0 == newCount) {
        delete this;
    }
    return newCount;
}

HRESULT StreamFontFileLoader::CreateStreamFromKey(
    void const* fontFileReferenceKey,
    UINT32 fontFileReferenceKeySize,
    IDWriteFontFileStream** fontFileStream)
{
    SkTScopedComPtr<SkDWriteFontFileStreamWrapper> stream;
    HR(SkDWriteFontFileStreamWrapper::Create(fStream, &stream));
    *fontFileStream = stream.release();
    return S_OK;
}

class StreamFontFileEnumerator : public IDWriteFontFileEnumerator {
public:
    // IUnknown methods
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject);
    virtual ULONG STDMETHODCALLTYPE AddRef();
    virtual ULONG STDMETHODCALLTYPE Release();

    // IDWriteFontFileEnumerator methods
    virtual HRESULT STDMETHODCALLTYPE MoveNext(BOOL* hasCurrentFile);
    virtual HRESULT STDMETHODCALLTYPE GetCurrentFontFile(IDWriteFontFile** fontFile);

    static HRESULT Create(IDWriteFactory* factory, IDWriteFontFileLoader* fontFileLoader,
                          StreamFontFileEnumerator** streamFontFileEnumerator) {
        *streamFontFileEnumerator = new StreamFontFileEnumerator(factory, fontFileLoader);
        if (NULL == streamFontFileEnumerator) {
            return E_OUTOFMEMORY;
        }
        return S_OK;
    }
private:
    StreamFontFileEnumerator(IDWriteFactory* factory, IDWriteFontFileLoader* fontFileLoader);
    ULONG fRefCount;

    SkTScopedComPtr<IDWriteFactory> fFactory;
    SkTScopedComPtr<IDWriteFontFile> fCurrentFile;
    SkTScopedComPtr<IDWriteFontFileLoader> fFontFileLoader;
    bool fHasNext;
};

StreamFontFileEnumerator::StreamFontFileEnumerator(IDWriteFactory* factory,
                                                   IDWriteFontFileLoader* fontFileLoader)
    : fRefCount(1)
    , fFactory(SkRefComPtr(factory))
    , fCurrentFile()
    , fFontFileLoader(SkRefComPtr(fontFileLoader))
    , fHasNext(true)
{ }

HRESULT StreamFontFileEnumerator::QueryInterface(REFIID iid, void** ppvObject) {
    if (iid == IID_IUnknown || iid == __uuidof(IDWriteFontFileEnumerator)) {
        *ppvObject = this;
        AddRef();
        return S_OK;
    } else {
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }
}

ULONG StreamFontFileEnumerator::AddRef() {
    return InterlockedIncrement(&fRefCount);
}

ULONG StreamFontFileEnumerator::Release() {
    ULONG newCount = InterlockedDecrement(&fRefCount);
    if (0 == newCount) {
        delete this;
    }
    return newCount;
}

HRESULT StreamFontFileEnumerator::MoveNext(BOOL* hasCurrentFile) {
    *hasCurrentFile = FALSE;

    if (!fHasNext) {
        return S_OK;
    }
    fHasNext = false;

    UINT32 dummy = 0;
    HR(fFactory->CreateCustomFontFileReference(
            &dummy, //cannot be NULL
            sizeof(dummy), //even if this is 0
            fFontFileLoader.get(),
            &fCurrentFile));

    *hasCurrentFile = TRUE;
    return S_OK;
}

HRESULT StreamFontFileEnumerator::GetCurrentFontFile(IDWriteFontFile** fontFile) {
    if (fCurrentFile.get() == NULL) {
        *fontFile = NULL;
        return E_FAIL;
    }

    *fontFile = SkRefComPtr(fCurrentFile.get());
    return  S_OK;
}

class StreamFontCollectionLoader : public IDWriteFontCollectionLoader {
public:
    // IUnknown methods
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject);
    virtual ULONG STDMETHODCALLTYPE AddRef();
    virtual ULONG STDMETHODCALLTYPE Release();

    // IDWriteFontCollectionLoader methods
    virtual HRESULT STDMETHODCALLTYPE CreateEnumeratorFromKey(
        IDWriteFactory* factory,
        void const* collectionKey,
        UINT32 collectionKeySize,
        IDWriteFontFileEnumerator** fontFileEnumerator);

    static HRESULT Create(IDWriteFontFileLoader* fontFileLoader,
                          StreamFontCollectionLoader** streamFontCollectionLoader) {
        *streamFontCollectionLoader = new StreamFontCollectionLoader(fontFileLoader);
        if (NULL == streamFontCollectionLoader) {
            return E_OUTOFMEMORY;
        }
        return S_OK;
    }
private:
    StreamFontCollectionLoader(IDWriteFontFileLoader* fontFileLoader)
        : fRefCount(1)
        , fFontFileLoader(SkRefComPtr(fontFileLoader))
    { }

    ULONG fRefCount;
    SkTScopedComPtr<IDWriteFontFileLoader> fFontFileLoader;
};

HRESULT StreamFontCollectionLoader::QueryInterface(REFIID iid, void** ppvObject) {
    if (iid == IID_IUnknown || iid == __uuidof(IDWriteFontCollectionLoader)) {
        *ppvObject = this;
        AddRef();
        return S_OK;
    } else {
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }
}

ULONG StreamFontCollectionLoader::AddRef() {
    return InterlockedIncrement(&fRefCount);
}

ULONG StreamFontCollectionLoader::Release() {
    ULONG newCount = InterlockedDecrement(&fRefCount);
    if (0 == newCount) {
        delete this;
    }
    return newCount;
}

HRESULT StreamFontCollectionLoader::CreateEnumeratorFromKey(
    IDWriteFactory* factory,
    void const* collectionKey,
    UINT32 collectionKeySize,
    IDWriteFontFileEnumerator** fontFileEnumerator)
{
    SkTScopedComPtr<StreamFontFileEnumerator> enumerator;
    HR(StreamFontFileEnumerator::Create(factory, fFontFileLoader.get(), &enumerator));
    *fontFileEnumerator = enumerator.release();
    return S_OK;
}

///////////////////////////////////////////////////////////////////////////////

static SkTypeface::Style get_style(IDWriteFont* font) {
    int style = SkTypeface::kNormal;
    DWRITE_FONT_WEIGHT weight = font->GetWeight();
    if (DWRITE_FONT_WEIGHT_DEMI_BOLD <= weight) {
        style |= SkTypeface::kBold;
    }
    DWRITE_FONT_STYLE angle = font->GetStyle();
    if (DWRITE_FONT_STYLE_OBLIQUE == angle || DWRITE_FONT_STYLE_ITALIC == angle) {
        style |= SkTypeface::kItalic;
    }
    return static_cast<SkTypeface::Style>(style);
}

class DWriteFontTypeface : public SkTypeface {
private:
    DWriteFontTypeface(SkTypeface::Style style, SkFontID fontID,
                       IDWriteFactory* factory,
                       IDWriteFontFace* fontFace,
                       IDWriteFont* font,
                       IDWriteFontFamily* fontFamily,
                       StreamFontFileLoader* fontFileLoader = NULL,
                       IDWriteFontCollectionLoader* fontCollectionLoader = NULL)
        : SkTypeface(style, fontID, false)
        , fFactory(SkRefComPtr(factory))
        , fDWriteFontCollectionLoader(SkSafeRefComPtr(fontCollectionLoader))
        , fDWriteFontFileLoader(SkSafeRefComPtr(fontFileLoader))
        , fDWriteFontFamily(SkRefComPtr(fontFamily))
        , fDWriteFont(SkRefComPtr(font))
        , fDWriteFontFace(SkRefComPtr(fontFace))
    { }

public:
    SkTScopedComPtr<IDWriteFactory> fFactory;
    SkTScopedComPtr<IDWriteFontCollectionLoader> fDWriteFontCollectionLoader;
    SkTScopedComPtr<StreamFontFileLoader> fDWriteFontFileLoader;
    SkTScopedComPtr<IDWriteFontFamily> fDWriteFontFamily;
    SkTScopedComPtr<IDWriteFont> fDWriteFont;
    SkTScopedComPtr<IDWriteFontFace> fDWriteFontFace;

    static DWriteFontTypeface* Create(IDWriteFactory* factory,
                                      IDWriteFontFace* fontFace,
                                      IDWriteFont* font,
                                      IDWriteFontFamily* fontFamily,
                                      StreamFontFileLoader* fontFileLoader = NULL,
                                      IDWriteFontCollectionLoader* fontCollectionLoader = NULL) {
        SkTypeface::Style style = get_style(font);
        SkFontID fontID = SkTypefaceCache::NewFontID();
        return SkNEW_ARGS(DWriteFontTypeface, (style, fontID,
                                               factory, fontFace, font, fontFamily,
                                               fontFileLoader, fontCollectionLoader));
    }

protected:
    virtual void weak_dispose() const SK_OVERRIDE {
        if (fDWriteFontCollectionLoader.get()) {
            HRV(fFactory->UnregisterFontCollectionLoader(fDWriteFontCollectionLoader.get()));
        }
        if (fDWriteFontFileLoader.get()) {
            HRV(fFactory->UnregisterFontFileLoader(fDWriteFontFileLoader.get()));
        }

        //SkTypefaceCache::Remove(this);
        INHERITED::weak_dispose();
    }

    virtual SkStream* onOpenStream(int* ttcIndex) const SK_OVERRIDE;
    virtual SkScalerContext* onCreateScalerContext(const SkDescriptor*) const SK_OVERRIDE;
    virtual void onFilterRec(SkScalerContextRec*) const SK_OVERRIDE;
    virtual SkAdvancedTypefaceMetrics* onGetAdvancedTypefaceMetrics(
                                SkAdvancedTypefaceMetrics::PerGlyphInfo,
                                const uint32_t*, uint32_t) const SK_OVERRIDE;
    virtual void onGetFontDescriptor(SkFontDescriptor*, bool*) const SK_OVERRIDE;
    virtual int onCharsToGlyphs(const void* chars, Encoding encoding,
                                uint16_t glyphs[], int glyphCount) const SK_OVERRIDE;
    virtual int onCountGlyphs() const SK_OVERRIDE;
    virtual int onGetUPEM() const SK_OVERRIDE;
    virtual SkTypeface::LocalizedStrings* onCreateFamilyNameIterator() const SK_OVERRIDE;
    virtual int onGetTableTags(SkFontTableTag tags[]) const SK_OVERRIDE;
    virtual size_t onGetTableData(SkFontTableTag, size_t offset,
                                  size_t length, void* data) const SK_OVERRIDE;

private:
    typedef SkTypeface INHERITED;
};

class SkScalerContext_DW : public SkScalerContext {
public:
    SkScalerContext_DW(DWriteFontTypeface*, const SkDescriptor* desc);
    virtual ~SkScalerContext_DW();

protected:
    virtual unsigned generateGlyphCount() SK_OVERRIDE;
    virtual uint16_t generateCharToGlyph(SkUnichar uni) SK_OVERRIDE;
    virtual void generateAdvance(SkGlyph* glyph) SK_OVERRIDE;
    virtual void generateMetrics(SkGlyph* glyph) SK_OVERRIDE;
    virtual void generateImage(const SkGlyph& glyph) SK_OVERRIDE;
    virtual void generatePath(const SkGlyph& glyph, SkPath* path) SK_OVERRIDE;
    virtual void generateFontMetrics(SkPaint::FontMetrics* mX,
                                     SkPaint::FontMetrics* mY) SK_OVERRIDE;

private:
    const void* drawDWMask(const SkGlyph& glyph);

    SkTDArray<uint8_t> fBits;
    /** The total matrix without the text height scale. */
    SkMatrix fSkXform;
    /** The total matrix without the text height scale. */
    DWRITE_MATRIX fXform;
    /** The non-rotational part of total matrix without the text height scale.
     *  This is used to find the magnitude of gdi compatible advances.
     */
    DWRITE_MATRIX fGsA;
    /** The inverse of the rotational part of the total matrix.
     *  This is used to find the direction of gdi compatible advances.
     */
    SkMatrix fG_inv;
    /** The text size to render with. */
    SkScalar fTextSizeRender;
    /** The text size to measure with. */
    SkScalar fTextSizeMeasure;
    SkAutoTUnref<DWriteFontTypeface> fTypeface;
    int fGlyphCount;
    DWRITE_RENDERING_MODE fRenderingMode;
    DWRITE_TEXTURE_TYPE fTextureType;
    DWRITE_MEASURING_MODE fMeasuringMode;
};

static bool are_same(IUnknown* a, IUnknown* b) {
    SkTScopedComPtr<IUnknown> iunkA;
    if (FAILED(a->QueryInterface(&iunkA))) {
        return false;
    }

    SkTScopedComPtr<IUnknown> iunkB;
    if (FAILED(b->QueryInterface(&iunkB))) {
        return false;
    }

    return iunkA.get() == iunkB.get();
}
static bool FindByDWriteFont(SkTypeface* face, SkTypeface::Style requestedStyle, void* ctx) {
    //Check to see if the two fonts are identical.
    DWriteFontTypeface* dwFace = reinterpret_cast<DWriteFontTypeface*>(face);
    IDWriteFont* dwFont = reinterpret_cast<IDWriteFont*>(ctx);
    if (are_same(dwFace->fDWriteFont.get(), dwFont)) {
        return true;
    }

    //Check if the two fonts share the same loader and have the same key.
    SkTScopedComPtr<IDWriteFontFace> dwFaceFontFace;
    SkTScopedComPtr<IDWriteFontFace> dwFontFace;
    HRB(dwFace->fDWriteFont->CreateFontFace(&dwFaceFontFace));
    HRB(dwFont->CreateFontFace(&dwFontFace));
    if (are_same(dwFaceFontFace.get(), dwFontFace.get())) {
        return true;
    }

    UINT32 dwFaceNumFiles;
    UINT32 dwNumFiles;
    HRB(dwFaceFontFace->GetFiles(&dwFaceNumFiles, NULL));
    HRB(dwFontFace->GetFiles(&dwNumFiles, NULL));
    if (dwFaceNumFiles != dwNumFiles) {
        return false;
    }

    SkTScopedComPtr<IDWriteFontFile> dwFaceFontFile;
    SkTScopedComPtr<IDWriteFontFile> dwFontFile;
    HRB(dwFaceFontFace->GetFiles(&dwFaceNumFiles, &dwFaceFontFile));
    HRB(dwFontFace->GetFiles(&dwNumFiles, &dwFontFile));

    //for (each file) { //we currently only admit fonts from one file.
    SkTScopedComPtr<IDWriteFontFileLoader> dwFaceFontFileLoader;
    SkTScopedComPtr<IDWriteFontFileLoader> dwFontFileLoader;
    HRB(dwFaceFontFile->GetLoader(&dwFaceFontFileLoader));
    HRB(dwFontFile->GetLoader(&dwFontFileLoader));
    if (!are_same(dwFaceFontFileLoader.get(), dwFontFileLoader.get())) {
        return false;
    }
    //}

    const void* dwFaceFontRefKey;
    UINT32 dwFaceFontRefKeySize;
    const void* dwFontRefKey;
    UINT32 dwFontRefKeySize;
    HRB(dwFaceFontFile->GetReferenceKey(&dwFaceFontRefKey, &dwFaceFontRefKeySize));
    HRB(dwFontFile->GetReferenceKey(&dwFontRefKey, &dwFontRefKeySize));
    if (dwFaceFontRefKeySize != dwFontRefKeySize) {
        return false;
    }
    if (0 != memcmp(dwFaceFontRefKey, dwFontRefKey, dwFontRefKeySize)) {
        return false;
    }

    //TODO: better means than comparing name strings?
    //NOTE: .tfc and fake bold/italic will end up here.
    SkTScopedComPtr<IDWriteFontFamily> dwFaceFontFamily;
    SkTScopedComPtr<IDWriteFontFamily> dwFontFamily;
    HRB(dwFace->fDWriteFont->GetFontFamily(&dwFaceFontFamily));
    HRB(dwFont->GetFontFamily(&dwFontFamily));

    SkTScopedComPtr<IDWriteLocalizedStrings> dwFaceFontFamilyNames;
    SkTScopedComPtr<IDWriteLocalizedStrings> dwFaceFontNames;
    HRB(dwFaceFontFamily->GetFamilyNames(&dwFaceFontFamilyNames));
    HRB(dwFace->fDWriteFont->GetFaceNames(&dwFaceFontNames));

    SkTScopedComPtr<IDWriteLocalizedStrings> dwFontFamilyNames;
    SkTScopedComPtr<IDWriteLocalizedStrings> dwFontNames;
    HRB(dwFontFamily->GetFamilyNames(&dwFontFamilyNames));
    HRB(dwFont->GetFaceNames(&dwFontNames));

    UINT32 dwFaceFontFamilyNameLength;
    UINT32 dwFaceFontNameLength;
    HRB(dwFaceFontFamilyNames->GetStringLength(0, &dwFaceFontFamilyNameLength));
    HRB(dwFaceFontNames->GetStringLength(0, &dwFaceFontNameLength));

    UINT32 dwFontFamilyNameLength;
    UINT32 dwFontNameLength;
    HRB(dwFontFamilyNames->GetStringLength(0, &dwFontFamilyNameLength));
    HRB(dwFontNames->GetStringLength(0, &dwFontNameLength));

    if (dwFaceFontFamilyNameLength != dwFontFamilyNameLength ||
        dwFaceFontNameLength != dwFontNameLength)
    {
        return false;
    }

    SkSMallocWCHAR dwFaceFontFamilyNameChar(dwFaceFontFamilyNameLength+1);
    SkSMallocWCHAR dwFaceFontNameChar(dwFaceFontNameLength+1);
    HRB(dwFaceFontFamilyNames->GetString(0, dwFaceFontFamilyNameChar.get(), dwFaceFontFamilyNameLength+1));
    HRB(dwFaceFontNames->GetString(0, dwFaceFontNameChar.get(), dwFaceFontNameLength+1));

    SkSMallocWCHAR dwFontFamilyNameChar(dwFontFamilyNameLength+1);
    SkSMallocWCHAR dwFontNameChar(dwFontNameLength+1);
    HRB(dwFontFamilyNames->GetString(0, dwFontFamilyNameChar.get(), dwFontFamilyNameLength+1));
    HRB(dwFontNames->GetString(0, dwFontNameChar.get(), dwFontNameLength+1));

    return wcscmp(dwFaceFontFamilyNameChar.get(), dwFontFamilyNameChar.get()) == 0 &&
           wcscmp(dwFaceFontNameChar.get(), dwFontNameChar.get()) == 0;
}

class AutoDWriteTable {
public:
    AutoDWriteTable(IDWriteFontFace* fontFace, UINT32 beTag) : fFontFace(fontFace), fExists(FALSE) {
        // Any errors are ignored, user must check fExists anyway.
        fontFace->TryGetFontTable(beTag,
            reinterpret_cast<const void **>(&fData), &fSize, &fLock, &fExists);
    }
    ~AutoDWriteTable() {
        if (fExists) {
            fFontFace->ReleaseFontTable(fLock);
        }
    }

    const uint8_t* fData;
    UINT32 fSize;
    BOOL fExists;
private:
    // Borrowed reference, the user must ensure the fontFace stays alive.
    IDWriteFontFace* fFontFace;
    void* fLock;
};
template<typename T> class AutoTDWriteTable : public AutoDWriteTable {
public:
    static const UINT32 tag = DWRITE_MAKE_OPENTYPE_TAG(T::TAG0, T::TAG1, T::TAG2, T::TAG3);
    AutoTDWriteTable(IDWriteFontFace* fontFace) : AutoDWriteTable(fontFace, tag) { }

    const T* get() const { return reinterpret_cast<const T*>(fData); }
    const T* operator->() const { return reinterpret_cast<const T*>(fData); }
};

static bool hasBitmapStrike(DWriteFontTypeface* typeface, int size) {
    {
        AutoTDWriteTable<SkOTTableEmbeddedBitmapLocation> eblc(typeface->fDWriteFontFace.get());
        if (!eblc.fExists) {
            return false;
        }
        if (eblc.fSize < sizeof(SkOTTableEmbeddedBitmapLocation)) {
            return false;
        }
        if (eblc->version != SkOTTableEmbeddedBitmapLocation::version_initial) {
            return false;
        }

        uint32_t numSizes = SkEndianSwap32(eblc->numSizes);
        if (eblc.fSize < sizeof(SkOTTableEmbeddedBitmapLocation) +
                         sizeof(SkOTTableEmbeddedBitmapLocation::BitmapSizeTable) * numSizes)
        {
            return false;
        }

        const SkOTTableEmbeddedBitmapLocation::BitmapSizeTable* sizeTable =
                SkTAfter<const SkOTTableEmbeddedBitmapLocation::BitmapSizeTable>(eblc.get());
        for (uint32_t i = 0; i < numSizes; ++i, ++sizeTable) {
            if (sizeTable->ppemX == size && sizeTable->ppemY == size) {
                // TODO: determine if we should dig through IndexSubTableArray/IndexSubTable
                // to determine the actual number of glyphs with bitmaps.

                // TODO: Ensure that the bitmaps actually cover a significant portion of the strike.

                //TODO: Endure that the bitmaps are bi-level.
                if (sizeTable->endGlyphIndex >= sizeTable->startGlyphIndex + 3) {
                    return true;
                }
            }
        }
    }

    {
        AutoTDWriteTable<SkOTTableEmbeddedBitmapScaling> ebsc(typeface->fDWriteFontFace.get());
        if (!ebsc.fExists) {
            return false;
        }
        if (ebsc.fSize < sizeof(SkOTTableEmbeddedBitmapScaling)) {
            return false;
        }
        if (ebsc->version != SkOTTableEmbeddedBitmapScaling::version_initial) {
            return false;
        }

        uint32_t numSizes = SkEndianSwap32(ebsc->numSizes);
        if (ebsc.fSize < sizeof(SkOTTableEmbeddedBitmapScaling) +
                         sizeof(SkOTTableEmbeddedBitmapScaling::BitmapScaleTable) * numSizes)
        {
            return false;
        }

        const SkOTTableEmbeddedBitmapScaling::BitmapScaleTable* scaleTable =
                SkTAfter<const SkOTTableEmbeddedBitmapScaling::BitmapScaleTable>(ebsc.get());
        for (uint32_t i = 0; i < numSizes; ++i, ++scaleTable) {
            if (scaleTable->ppemX == size && scaleTable->ppemY == size) {
                // EBSC tables are normally only found in bitmap only fonts.
                return true;
            }
        }
    }

    return false;
}

static bool bothZero(SkScalar a, SkScalar b) {
    return 0 == a && 0 == b;
}

// returns false if there is any non-90-rotation or skew
static bool isAxisAligned(const SkScalerContext::Rec& rec) {
    return 0 == rec.fPreSkewX &&
           (bothZero(rec.fPost2x2[0][1], rec.fPost2x2[1][0]) ||
            bothZero(rec.fPost2x2[0][0], rec.fPost2x2[1][1]));
}

SkScalerContext_DW::SkScalerContext_DW(DWriteFontTypeface* typeface,
                                       const SkDescriptor* desc)
        : SkScalerContext(typeface, desc)
        , fTypeface(SkRef(typeface))
        , fGlyphCount(-1) {

    // In general, all glyphs should use CLEARTYPE_NATURAL_SYMMETRIC
    // except when bi-level rendering is requested or there are embedded
    // bi-level bitmaps (and the embedded bitmap flag is set and no rotation).
    //
    // DirectWrite's IDWriteFontFace::GetRecommendedRenderingMode does not do
    // this. As a result, determine the actual size of the text and then see if
    // there are any embedded bi-level bitmaps of that size. If there are, then
    // force bitmaps by requesting bi-level rendering.
    //
    // FreeType allows for separate ppemX and ppemY, but DirectWrite assumes
    // square pixels and only uses ppemY. Therefore the transform must track any
    // non-uniform x-scale.
    //
    // Also, rotated glyphs should have the same absolute advance widths as
    // horizontal glyphs and the subpixel flag should not affect glyph shapes.

    // A is the total matrix.
    SkMatrix A;
    fRec.getSingleMatrix(&A);

    // h is where A maps the horizontal baseline.
    SkPoint h = SkPoint::Make(SK_Scalar1, 0);
    A.mapPoints(&h, 1);

    // G is the Givens Matrix for A (rotational matrix where GA[0][1] == 0).
    SkMatrix G;
    SkComputeGivensRotation(h, &G);

    // GA is the matrix A with rotation removed.
    SkMatrix GA(G);
    GA.preConcat(A);

    // realTextSize is the actual device size we want (as opposed to the size the user requested).
    // gdiTextSize is the size we request when GDI compatible.
    // If the scale is negative, this means the matrix will do the flip anyway.
    SkScalar realTextSize = SkScalarAbs(GA.get(SkMatrix::kMScaleY));
    // Due to floating point math, the lower bits are suspect. Round carefully.
    SkScalar roundedTextSize = SkScalarRoundToScalar(realTextSize * 64.0f) / 64.0f;
    SkScalar gdiTextSize = SkScalarFloorToScalar(roundedTextSize);
    if (gdiTextSize == 0) {
        gdiTextSize = SK_Scalar1;
    }

    bool hasBitmap = fRec.fFlags & SkScalerContext::kEmbeddedBitmapText_Flag &&
                     hasBitmapStrike(typeface, SkScalarTruncToInt(gdiTextSize));
    bool axisAligned = isAxisAligned(fRec);
    bool isBiLevel = SkMask::kBW_Format == fRec.fMaskFormat || (hasBitmap && axisAligned);

    if (isBiLevel) {
        fTextSizeRender = gdiTextSize;
        fRenderingMode = DWRITE_RENDERING_MODE_ALIASED;
        fTextureType = DWRITE_TEXTURE_ALIASED_1x1;
        fTextSizeMeasure = gdiTextSize;
        fMeasuringMode = DWRITE_MEASURING_MODE_GDI_CLASSIC;
    } else if (hasBitmap) {
        // If rotated but the horizontal text would have used a bitmap,
        // render high quality rotated glyphs using the bitmap metrics.
        fTextSizeRender = gdiTextSize;
        fRenderingMode = DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC;
        fTextureType = DWRITE_TEXTURE_CLEARTYPE_3x1;
        fTextSizeMeasure = gdiTextSize;
        fMeasuringMode = DWRITE_MEASURING_MODE_GDI_CLASSIC;
    } else {
        fTextSizeRender = realTextSize;
        fRenderingMode = DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC;
        fTextureType = DWRITE_TEXTURE_CLEARTYPE_3x1;
        fTextSizeMeasure = realTextSize;
        fMeasuringMode = DWRITE_MEASURING_MODE_NATURAL;
    }

    if (this->isSubpixel()) {
        fTextSizeMeasure = realTextSize;
        fMeasuringMode = DWRITE_MEASURING_MODE_NATURAL;
    }

    // Remove the realTextSize, as that is the text height scale currently in A.
    SkScalar scale = SkScalarInvert(realTextSize);

    // fSkXform is the total matrix A without the text height scale.
    fSkXform = A;
    fSkXform.preScale(scale, scale); //remove the text height scale.

    fXform.m11 = SkScalarToFloat(fSkXform.getScaleX());
    fXform.m12 = SkScalarToFloat(fSkXform.getSkewY());
    fXform.m21 = SkScalarToFloat(fSkXform.getSkewX());
    fXform.m22 = SkScalarToFloat(fSkXform.getScaleY());
    fXform.dx = 0;
    fXform.dy = 0;

    // GsA is the non-rotational part of A without the text height scale.
    SkMatrix GsA(GA);
    GsA.preScale(scale, scale); //remove text height scale, G is rotational so reorders with scale.

    fGsA.m11 = SkScalarToFloat(GsA.get(SkMatrix::kMScaleX));
    fGsA.m12 = SkScalarToFloat(GsA.get(SkMatrix::kMSkewY)); // This should be ~0.
    fGsA.m21 = SkScalarToFloat(GsA.get(SkMatrix::kMSkewX));
    fGsA.m22 = SkScalarToFloat(GsA.get(SkMatrix::kMScaleY));

    // fG_inv is G inverse, which is fairly simple since G is 2x2 rotational.
    fG_inv.setAll(G.get(SkMatrix::kMScaleX), -G.get(SkMatrix::kMSkewX), G.get(SkMatrix::kMTransX),
                  -G.get(SkMatrix::kMSkewY), G.get(SkMatrix::kMScaleY), G.get(SkMatrix::kMTransY),
                  G.get(SkMatrix::kMPersp0), G.get(SkMatrix::kMPersp1), G.get(SkMatrix::kMPersp2));
}

SkScalerContext_DW::~SkScalerContext_DW() {
}

unsigned SkScalerContext_DW::generateGlyphCount() {
    if (fGlyphCount < 0) {
        fGlyphCount = fTypeface->fDWriteFontFace->GetGlyphCount();
    }
    return fGlyphCount;
}

uint16_t SkScalerContext_DW::generateCharToGlyph(SkUnichar uni) {
    uint16_t index = 0;
    fTypeface->fDWriteFontFace->GetGlyphIndices(reinterpret_cast<UINT32*>(&uni), 1, &index);
    return index;
}

void SkScalerContext_DW::generateAdvance(SkGlyph* glyph) {
    //Delta is the difference between the right/left side bearing metric
    //and where the right/left side bearing ends up after hinting.
    //DirectWrite does not provide this information.
    glyph->fRsbDelta = 0;
    glyph->fLsbDelta = 0;

    glyph->fAdvanceX = 0;
    glyph->fAdvanceY = 0;

    uint16_t glyphId = glyph->getGlyphID();
    DWRITE_GLYPH_METRICS gm;

    if (DWRITE_MEASURING_MODE_GDI_CLASSIC == fMeasuringMode ||
        DWRITE_MEASURING_MODE_GDI_NATURAL == fMeasuringMode)
    {
        HRVM(fTypeface->fDWriteFontFace->GetGdiCompatibleGlyphMetrics(
                 fTextSizeMeasure,
                 1.0f, // pixelsPerDip
                 &fGsA,
                 DWRITE_MEASURING_MODE_GDI_NATURAL == fMeasuringMode,
                 &glyphId, 1,
                 &gm),
             "Could not get gdi compatible glyph metrics.");
    } else {
        HRVM(fTypeface->fDWriteFontFace->GetDesignGlyphMetrics(&glyphId, 1, &gm),
             "Could not get design metrics.");
    }

    DWRITE_FONT_METRICS dwfm;
    fTypeface->fDWriteFontFace->GetMetrics(&dwfm);
    SkScalar advanceX = SkScalarMulDiv(fTextSizeMeasure,
                                       SkIntToScalar(gm.advanceWidth),
                                       SkIntToScalar(dwfm.designUnitsPerEm));

    if (!this->isSubpixel()) {
        advanceX = SkScalarRoundToScalar(advanceX);
    }

    SkVector vecs[1] = { { advanceX, 0 } };
    if (DWRITE_MEASURING_MODE_GDI_CLASSIC == fMeasuringMode ||
        DWRITE_MEASURING_MODE_GDI_NATURAL == fMeasuringMode)
    {
        fG_inv.mapVectors(vecs, SK_ARRAY_COUNT(vecs));
    } else {
        fSkXform.mapVectors(vecs, SK_ARRAY_COUNT(vecs));
    }

    glyph->fAdvanceX = SkScalarToFixed(vecs[0].fX);
    glyph->fAdvanceY = SkScalarToFixed(vecs[0].fY);
}

void SkScalerContext_DW::generateMetrics(SkGlyph* glyph) {
    glyph->fWidth = 0;

    this->generateAdvance(glyph);

    //Measure raster size.
    fXform.dx = SkFixedToFloat(glyph->getSubXFixed());
    fXform.dy = SkFixedToFloat(glyph->getSubYFixed());

    FLOAT advance = 0;

    UINT16 glyphId = glyph->getGlyphID();

    DWRITE_GLYPH_OFFSET offset;
    offset.advanceOffset = 0.0f;
    offset.ascenderOffset = 0.0f;

    DWRITE_GLYPH_RUN run;
    run.glyphCount = 1;
    run.glyphAdvances = &advance;
    run.fontFace = fTypeface->fDWriteFontFace.get();
    run.fontEmSize = SkScalarToFloat(fTextSizeRender);
    run.bidiLevel = 0;
    run.glyphIndices = &glyphId;
    run.isSideways = FALSE;
    run.glyphOffsets = &offset;

    SkTScopedComPtr<IDWriteGlyphRunAnalysis> glyphRunAnalysis;
    HRVM(fTypeface->fFactory->CreateGlyphRunAnalysis(
             &run,
             1.0f, // pixelsPerDip,
             &fXform,
             fRenderingMode,
             fMeasuringMode,
             0.0f, // baselineOriginX,
             0.0f, // baselineOriginY,
             &glyphRunAnalysis),
         "Could not create glyph run analysis.");

    RECT bbox;
    HRVM(glyphRunAnalysis->GetAlphaTextureBounds(fTextureType, &bbox),
         "Could not get texture bounds.");

    glyph->fWidth = SkToU16(bbox.right - bbox.left);
    glyph->fHeight = SkToU16(bbox.bottom - bbox.top);
    glyph->fLeft = SkToS16(bbox.left);
    glyph->fTop = SkToS16(bbox.top);
}

void SkScalerContext_DW::generateFontMetrics(SkPaint::FontMetrics* mx,
                                             SkPaint::FontMetrics* my) {
    if (!(mx || my))
      return;

    if (mx) {
        sk_bzero(mx, sizeof(*mx));
    }
    if (my) {
        sk_bzero(my, sizeof(*my));
    }

    DWRITE_FONT_METRICS dwfm;
    if (DWRITE_MEASURING_MODE_GDI_CLASSIC == fMeasuringMode ||
        DWRITE_MEASURING_MODE_GDI_NATURAL == fMeasuringMode)
    {
        fTypeface->fDWriteFontFace->GetGdiCompatibleMetrics(
             fTextSizeRender,
             1.0f, // pixelsPerDip
             &fXform,
             &dwfm);
    } else {
        fTypeface->fDWriteFontFace->GetMetrics(&dwfm);
    }

    SkScalar upem = SkIntToScalar(dwfm.designUnitsPerEm);
    if (mx) {
        mx->fTop = -fTextSizeRender * SkIntToScalar(dwfm.ascent) / upem;
        mx->fAscent = mx->fTop;
        mx->fDescent = fTextSizeRender * SkIntToScalar(dwfm.descent) / upem;
        mx->fBottom = mx->fDescent;
        mx->fLeading = fTextSizeRender * SkIntToScalar(dwfm.lineGap) / upem;
        mx->fXHeight = fTextSizeRender * SkIntToScalar(dwfm.xHeight) / upem;
        mx->fUnderlineThickness = fTextSizeRender * SkIntToScalar(dwfm.underlineThickness) / upem;
        mx->fUnderlinePosition = -(fTextSizeRender * SkIntToScalar(dwfm.underlinePosition) / upem);

        mx->fFlags |= SkPaint::FontMetrics::kUnderlineThinknessIsValid_Flag;
        mx->fFlags |= SkPaint::FontMetrics::kUnderlinePositionIsValid_Flag;
    }

    if (my) {
        my->fTop = -fTextSizeRender * SkIntToScalar(dwfm.ascent) / upem;
        my->fAscent = my->fTop;
        my->fDescent = fTextSizeRender * SkIntToScalar(dwfm.descent) / upem;
        my->fBottom = my->fDescent;
        my->fLeading = fTextSizeRender * SkIntToScalar(dwfm.lineGap) / upem;
        my->fXHeight = fTextSizeRender * SkIntToScalar(dwfm.xHeight) / upem;
        my->fUnderlineThickness = fTextSizeRender * SkIntToScalar(dwfm.underlineThickness) / upem;
        my->fUnderlinePosition = -(fTextSizeRender * SkIntToScalar(dwfm.underlinePosition) / upem);

        my->fFlags |= SkPaint::FontMetrics::kUnderlineThinknessIsValid_Flag;
        my->fFlags |= SkPaint::FontMetrics::kUnderlinePositionIsValid_Flag;
    }
}

///////////////////////////////////////////////////////////////////////////////

#include "SkColorPriv.h"

static void bilevel_to_bw(const uint8_t* SK_RESTRICT src, const SkGlyph& glyph) {
    const int width = glyph.fWidth;
    const size_t dstRB = (width + 7) >> 3;
    uint8_t* SK_RESTRICT dst = static_cast<uint8_t*>(glyph.fImage);

    int byteCount = width >> 3;
    int bitCount = width & 7;

    for (int y = 0; y < glyph.fHeight; ++y) {
        if (byteCount > 0) {
            for (int i = 0; i < byteCount; ++i) {
                unsigned byte = 0;
                byte |= src[0] & (1 << 7);
                byte |= src[1] & (1 << 6);
                byte |= src[2] & (1 << 5);
                byte |= src[3] & (1 << 4);
                byte |= src[4] & (1 << 3);
                byte |= src[5] & (1 << 2);
                byte |= src[6] & (1 << 1);
                byte |= src[7] & (1 << 0);
                dst[i] = byte;
                src += 8;
            }
        }
        if (bitCount > 0) {
            unsigned byte = 0;
            unsigned mask = 0x80;
            for (int i = 0; i < bitCount; i++) {
                byte |= (src[i]) & mask;
                mask >>= 1;
            }
            dst[byteCount] = byte;
        }
        src += bitCount;
        dst += dstRB;
    }
}

template<bool APPLY_PREBLEND>
static void rgb_to_a8(const uint8_t* SK_RESTRICT src, const SkGlyph& glyph, const uint8_t* table8) {
    const size_t dstRB = glyph.rowBytes();
    const U16CPU width = glyph.fWidth;
    uint8_t* SK_RESTRICT dst = static_cast<uint8_t*>(glyph.fImage);

    for (U16CPU y = 0; y < glyph.fHeight; y++) {
        for (U16CPU i = 0; i < width; i++) {
            U8CPU r = *(src++);
            U8CPU g = *(src++);
            U8CPU b = *(src++);
            dst[i] = sk_apply_lut_if<APPLY_PREBLEND>((r + g + b) / 3, table8);
        }
        dst = (uint8_t*)((char*)dst + dstRB);
    }
}

template<bool APPLY_PREBLEND>
static void rgb_to_lcd16(const uint8_t* SK_RESTRICT src, const SkGlyph& glyph,
                         const uint8_t* tableR, const uint8_t* tableG, const uint8_t* tableB) {
    const size_t dstRB = glyph.rowBytes();
    const U16CPU width = glyph.fWidth;
    uint16_t* SK_RESTRICT dst = static_cast<uint16_t*>(glyph.fImage);

    for (U16CPU y = 0; y < glyph.fHeight; y++) {
        for (U16CPU i = 0; i < width; i++) {
            U8CPU r = sk_apply_lut_if<APPLY_PREBLEND>(*(src++), tableR);
            U8CPU g = sk_apply_lut_if<APPLY_PREBLEND>(*(src++), tableG);
            U8CPU b = sk_apply_lut_if<APPLY_PREBLEND>(*(src++), tableB);
            dst[i] = SkPack888ToRGB16(r, g, b);
        }
        dst = (uint16_t*)((char*)dst + dstRB);
    }
}

template<bool APPLY_PREBLEND>
static void rgb_to_lcd32(const uint8_t* SK_RESTRICT src, const SkGlyph& glyph,
                         const uint8_t* tableR, const uint8_t* tableG, const uint8_t* tableB) {
    const size_t dstRB = glyph.rowBytes();
    const U16CPU width = glyph.fWidth;
    SkPMColor* SK_RESTRICT dst = static_cast<SkPMColor*>(glyph.fImage);

    for (U16CPU y = 0; y < glyph.fHeight; y++) {
        for (U16CPU i = 0; i < width; i++) {
            U8CPU r = sk_apply_lut_if<APPLY_PREBLEND>(*(src++), tableR);
            U8CPU g = sk_apply_lut_if<APPLY_PREBLEND>(*(src++), tableG);
            U8CPU b = sk_apply_lut_if<APPLY_PREBLEND>(*(src++), tableB);
            dst[i] = SkPackARGB32(0xFF, r, g, b);
        }
        dst = (SkPMColor*)((char*)dst + dstRB);
    }
}

const void* SkScalerContext_DW::drawDWMask(const SkGlyph& glyph) {
    int sizeNeeded = glyph.fWidth * glyph.fHeight;
    if (DWRITE_RENDERING_MODE_ALIASED != fRenderingMode) {
        sizeNeeded *= 3;
    }
    if (sizeNeeded > fBits.count()) {
        fBits.setCount(sizeNeeded);
    }

    // erase
    memset(fBits.begin(), 0, sizeNeeded);

    fXform.dx = SkFixedToFloat(glyph.getSubXFixed());
    fXform.dy = SkFixedToFloat(glyph.getSubYFixed());

    FLOAT advance = 0.0f;

    UINT16 index = glyph.getGlyphID();

    DWRITE_GLYPH_OFFSET offset;
    offset.advanceOffset = 0.0f;
    offset.ascenderOffset = 0.0f;

    DWRITE_GLYPH_RUN run;
    run.glyphCount = 1;
    run.glyphAdvances = &advance;
    run.fontFace = fTypeface->fDWriteFontFace.get();
    run.fontEmSize = SkScalarToFloat(fTextSizeRender);
    run.bidiLevel = 0;
    run.glyphIndices = &index;
    run.isSideways = FALSE;
    run.glyphOffsets = &offset;

    SkTScopedComPtr<IDWriteGlyphRunAnalysis> glyphRunAnalysis;
    HRNM(fTypeface->fFactory->CreateGlyphRunAnalysis(&run,
                                          1.0f, // pixelsPerDip,
                                          &fXform,
                                          fRenderingMode,
                                          fMeasuringMode,
                                          0.0f, // baselineOriginX,
                                          0.0f, // baselineOriginY,
                                          &glyphRunAnalysis),
         "Could not create glyph run analysis.");

    //NOTE: this assumes that the glyph has already been measured
    //with an exact same glyph run analysis.
    RECT bbox;
    bbox.left = glyph.fLeft;
    bbox.top = glyph.fTop;
    bbox.right = glyph.fLeft + glyph.fWidth;
    bbox.bottom = glyph.fTop + glyph.fHeight;
    HRNM(glyphRunAnalysis->CreateAlphaTexture(fTextureType,
                                              &bbox,
                                              fBits.begin(),
                                              sizeNeeded),
         "Could not draw mask.");
    return fBits.begin();
}

void SkScalerContext_DW::generateImage(const SkGlyph& glyph) {
    //Create the mask.
    const void* bits = this->drawDWMask(glyph);
    if (!bits) {
        sk_bzero(glyph.fImage, glyph.computeImageSize());
        return;
    }

    //Copy the mask into the glyph.
    const uint8_t* src = (const uint8_t*)bits;
    if (DWRITE_RENDERING_MODE_ALIASED == fRenderingMode) {
        bilevel_to_bw(src, glyph);
        const_cast<SkGlyph&>(glyph).fMaskFormat = SkMask::kBW_Format;
    } else if (!isLCD(fRec)) {
        if (fPreBlend.isApplicable()) {
            rgb_to_a8<true>(src, glyph, fPreBlend.fG);
        } else {
            rgb_to_a8<false>(src, glyph, fPreBlend.fG);
        }
    } else if (SkMask::kLCD16_Format == glyph.fMaskFormat) {
        if (fPreBlend.isApplicable()) {
            rgb_to_lcd16<true>(src, glyph, fPreBlend.fR, fPreBlend.fG, fPreBlend.fB);
        } else {
            rgb_to_lcd16<false>(src, glyph, fPreBlend.fR, fPreBlend.fG, fPreBlend.fB);
        }
    } else {
        SkASSERT(SkMask::kLCD32_Format == glyph.fMaskFormat);
        if (fPreBlend.isApplicable()) {
            rgb_to_lcd32<true>(src, glyph, fPreBlend.fR, fPreBlend.fG, fPreBlend.fB);
        } else {
            rgb_to_lcd32<false>(src, glyph, fPreBlend.fR, fPreBlend.fG, fPreBlend.fB);
        }
    }
}

void SkScalerContext_DW::generatePath(const SkGlyph& glyph, SkPath* path) {
    SkASSERT(&glyph && path);

    path->reset();

    SkTScopedComPtr<IDWriteGeometrySink> geometryToPath;
    HRVM(SkDWriteGeometrySink::Create(path, &geometryToPath),
         "Could not create geometry to path converter.");
    uint16_t glyphId = glyph.getGlyphID();
    //TODO: convert to<->from DIUs? This would make a difference if hinting.
    //It may not be needed, it appears that DirectWrite only hints at em size.
    HRVM(fTypeface->fDWriteFontFace->GetGlyphRunOutline(SkScalarToFloat(fTextSizeRender),
                                       &glyphId,
                                       NULL, //advances
                                       NULL, //offsets
                                       1, //num glyphs
                                       FALSE, //sideways
                                       FALSE, //rtl
                                       geometryToPath.get()),
         "Could not create glyph outline.");

    path->transform(fSkXform);
}

void DWriteFontTypeface::onGetFontDescriptor(SkFontDescriptor* desc,
                                             bool* isLocalStream) const {
    // Get the family name.
    SkTScopedComPtr<IDWriteLocalizedStrings> dwFamilyNames;
    HRV(fDWriteFontFamily->GetFamilyNames(&dwFamilyNames));

    UINT32 dwFamilyNamesLength;
    HRV(dwFamilyNames->GetStringLength(0, &dwFamilyNamesLength));

    SkSMallocWCHAR dwFamilyNameChar(dwFamilyNamesLength+1);
    HRV(dwFamilyNames->GetString(0, dwFamilyNameChar.get(), dwFamilyNamesLength+1));

    SkString utf8FamilyName;
    HRV(sk_wchar_to_skstring(dwFamilyNameChar.get(), &utf8FamilyName));

    desc->setFamilyName(utf8FamilyName.c_str());
    *isLocalStream = SkToBool(fDWriteFontFileLoader.get());
}

static SkUnichar next_utf8(const void** chars) {
    return SkUTF8_NextUnichar((const char**)chars);
}

static SkUnichar next_utf16(const void** chars) {
    return SkUTF16_NextUnichar((const uint16_t**)chars);
}

static SkUnichar next_utf32(const void** chars) {
    const SkUnichar** uniChars = (const SkUnichar**)chars;
    SkUnichar uni = **uniChars;
    *uniChars += 1;
    return uni;
}

typedef SkUnichar (*EncodingProc)(const void**);

static EncodingProc find_encoding_proc(SkTypeface::Encoding enc) {
    static const EncodingProc gProcs[] = {
        next_utf8, next_utf16, next_utf32
    };
    SkASSERT((size_t)enc < SK_ARRAY_COUNT(gProcs));
    return gProcs[enc];
}

int DWriteFontTypeface::onCharsToGlyphs(const void* chars, Encoding encoding,
                                        uint16_t glyphs[], int glyphCount) const
{
    if (NULL == glyphs) {
        EncodingProc next_ucs4_proc = find_encoding_proc(encoding);
        for (int i = 0; i < glyphCount; ++i) {
            const SkUnichar c = next_ucs4_proc(&chars);
            BOOL exists;
            fDWriteFont->HasCharacter(c, &exists);
            if (!exists) {
                return i;
            }
        }
        return glyphCount;
    }

    switch (encoding) {
    case SkTypeface::kUTF8_Encoding:
    case SkTypeface::kUTF16_Encoding: {
        static const int scratchCount = 256;
        UINT32 scratch[scratchCount];
        EncodingProc next_ucs4_proc = find_encoding_proc(encoding);
        for (int baseGlyph = 0; baseGlyph < glyphCount; baseGlyph += scratchCount) {
            int glyphsLeft = glyphCount - baseGlyph;
            int limit = SkTMin(glyphsLeft, scratchCount);
            for (int i = 0; i < limit; ++i) {
                scratch[i] = next_ucs4_proc(&chars);
            }
            fDWriteFontFace->GetGlyphIndices(scratch, limit, &glyphs[baseGlyph]);
        }
        break;
    }
    case SkTypeface::kUTF32_Encoding: {
        const UINT32* utf32 = reinterpret_cast<const UINT32*>(chars);
        fDWriteFontFace->GetGlyphIndices(utf32, glyphCount, glyphs);
        break;
    }
    default:
        SK_CRASH();
    }

    for (int i = 0; i < glyphCount; ++i) {
        if (0 == glyphs[i]) {
            return i;
        }
    }
    return glyphCount;
}

int DWriteFontTypeface::onCountGlyphs() const {
    return fDWriteFontFace->GetGlyphCount();
}

int DWriteFontTypeface::onGetUPEM() const {
    DWRITE_FONT_METRICS metrics;
    fDWriteFontFace->GetMetrics(&metrics);
    return metrics.designUnitsPerEm;
}

class LocalizedStrings_IDWriteLocalizedStrings : public SkTypeface::LocalizedStrings {
public:
    /** Takes ownership of the IDWriteLocalizedStrings. */
    explicit LocalizedStrings_IDWriteLocalizedStrings(IDWriteLocalizedStrings* strings)
        : fIndex(0), fStrings(strings)
    { }

    virtual bool next(SkTypeface::LocalizedString* localizedString) SK_OVERRIDE {
        if (fIndex >= fStrings->GetCount()) {
            return false;
        }

        // String
        UINT32 stringLength;
        HRBM(fStrings->GetStringLength(fIndex, &stringLength), "Could not get string length.");
        stringLength += 1;

        SkSMallocWCHAR wString(stringLength);
        HRBM(fStrings->GetString(fIndex, wString.get(), stringLength), "Could not get string.");

        HRB(sk_wchar_to_skstring(wString.get(), &localizedString->fString));

        // Locale
        UINT32 localeLength;
        HRBM(fStrings->GetLocaleNameLength(fIndex, &localeLength), "Could not get locale length.");
        localeLength += 1;

        SkSMallocWCHAR wLocale(localeLength);
        HRBM(fStrings->GetLocaleName(fIndex, wLocale.get(), localeLength), "Could not get locale.");

        HRB(sk_wchar_to_skstring(wLocale.get(), &localizedString->fLanguage));

        ++fIndex;
        return true;
    }

private:
    UINT32 fIndex;
    SkTScopedComPtr<IDWriteLocalizedStrings> fStrings;
};

SkTypeface::LocalizedStrings* DWriteFontTypeface::onCreateFamilyNameIterator() const {
    SkTScopedComPtr<IDWriteLocalizedStrings> familyNames;
    HRNM(fDWriteFontFamily->GetFamilyNames(&familyNames), "Could not obtain family names.");

    return new LocalizedStrings_IDWriteLocalizedStrings(familyNames.release());
}

int DWriteFontTypeface::onGetTableTags(SkFontTableTag tags[]) const {
    DWRITE_FONT_FACE_TYPE type = fDWriteFontFace->GetType();
    if (type != DWRITE_FONT_FACE_TYPE_CFF &&
        type != DWRITE_FONT_FACE_TYPE_TRUETYPE &&
        type != DWRITE_FONT_FACE_TYPE_TRUETYPE_COLLECTION)
    {
        return 0;
    }

    int ttcIndex;
    SkAutoTUnref<SkStream> stream(this->openStream(&ttcIndex));
    return stream.get() ? SkFontStream::GetTableTags(stream, ttcIndex, tags) : 0;
}

size_t DWriteFontTypeface::onGetTableData(SkFontTableTag tag, size_t offset,
                                          size_t length, void* data) const
{
    AutoDWriteTable table(fDWriteFontFace.get(), SkEndian_SwapBE32(tag));
    if (!table.fExists) {
        return 0;
    }

    if (offset > table.fSize) {
        return 0;
    }
    size_t size = SkTMin(length, table.fSize - offset);
    if (NULL != data) {
        memcpy(data, table.fData + offset, size);
    }

    return size;
}

template <typename T> class SkAutoIDWriteUnregister {
public:
    SkAutoIDWriteUnregister(IDWriteFactory* factory, T* unregister)
        : fFactory(factory), fUnregister(unregister)
    { }

    ~SkAutoIDWriteUnregister() {
        if (fUnregister) {
            unregister(fFactory, fUnregister);
        }
    }

    T* detatch() {
        T* old = fUnregister;
        fUnregister = NULL;
        return old;
    }

private:
    HRESULT unregister(IDWriteFactory* factory, IDWriteFontFileLoader* unregister) {
        return factory->UnregisterFontFileLoader(unregister);
    }

    HRESULT unregister(IDWriteFactory* factory, IDWriteFontCollectionLoader* unregister) {
        return factory->UnregisterFontCollectionLoader(unregister);
    }

    IDWriteFactory* fFactory;
    T* fUnregister;
};

SkStream* DWriteFontTypeface::onOpenStream(int* ttcIndex) const {
    *ttcIndex = fDWriteFontFace->GetIndex();

    UINT32 numFiles;
    HRNM(fDWriteFontFace->GetFiles(&numFiles, NULL),
         "Could not get number of font files.");
    if (numFiles != 1) {
        return NULL;
    }

    SkTScopedComPtr<IDWriteFontFile> fontFile;
    HRNM(fDWriteFontFace->GetFiles(&numFiles, &fontFile), "Could not get font files.");

    const void* fontFileKey;
    UINT32 fontFileKeySize;
    HRNM(fontFile->GetReferenceKey(&fontFileKey, &fontFileKeySize),
         "Could not get font file reference key.");

    SkTScopedComPtr<IDWriteFontFileLoader> fontFileLoader;
    HRNM(fontFile->GetLoader(&fontFileLoader), "Could not get font file loader.");

    SkTScopedComPtr<IDWriteFontFileStream> fontFileStream;
    HRNM(fontFileLoader->CreateStreamFromKey(fontFileKey, fontFileKeySize,
                                             &fontFileStream),
         "Could not create font file stream.");

    return SkNEW_ARGS(SkDWriteFontFileStream, (fontFileStream.get()));
}

SkScalerContext* DWriteFontTypeface::onCreateScalerContext(const SkDescriptor* desc) const {
    return SkNEW_ARGS(SkScalerContext_DW, (const_cast<DWriteFontTypeface*>(this), desc));
}

void DWriteFontTypeface::onFilterRec(SkScalerContext::Rec* rec) const {
    if (rec->fFlags & SkScalerContext::kLCD_BGROrder_Flag ||
        rec->fFlags & SkScalerContext::kLCD_Vertical_Flag)
    {
        rec->fMaskFormat = SkMask::kA8_Format;
    }

    unsigned flagsWeDontSupport = SkScalerContext::kDevKernText_Flag |
                                  SkScalerContext::kForceAutohinting_Flag |
                                  SkScalerContext::kEmbolden_Flag |
                                  SkScalerContext::kLCD_BGROrder_Flag |
                                  SkScalerContext::kLCD_Vertical_Flag;
    rec->fFlags &= ~flagsWeDontSupport;

    SkPaint::Hinting h = rec->getHinting();
    // DirectWrite does not provide for hinting hints.
    h = SkPaint::kSlight_Hinting;
    rec->setHinting(h);

#if SK_FONT_HOST_USE_SYSTEM_SETTINGS
    IDWriteFactory* factory = get_dwrite_factory();
    if (factory != NULL) {
        SkTScopedComPtr<IDWriteRenderingParams> defaultRenderingParams;
        if (SUCCEEDED(factory->CreateRenderingParams(&defaultRenderingParams))) {
            float gamma = defaultRenderingParams->GetGamma();
            rec->setDeviceGamma(gamma);
            rec->setPaintGamma(gamma);

            rec->setContrast(defaultRenderingParams->GetEnhancedContrast());
        }
    }
#endif
}

///////////////////////////////////////////////////////////////////////////////
//PDF Support

using namespace skia_advanced_typeface_metrics_utils;

// Construct Glyph to Unicode table.
// Unicode code points that require conjugate pairs in utf16 are not
// supported.
// TODO(arthurhsu): Add support for conjugate pairs. It looks like that may
// require parsing the TTF cmap table (platform 4, encoding 12) directly instead
// of calling GetFontUnicodeRange().
// TODO(bungeman): This never does what anyone wants.
// What is really wanted is the text to glyphs mapping
static void populate_glyph_to_unicode(IDWriteFontFace* fontFace,
                                      const unsigned glyphCount,
                                      SkTDArray<SkUnichar>* glyphToUnicode) {
    HRESULT hr = S_OK;

    //Do this like free type instead
    UINT32 count = 0;
    for (UINT32 c = 0; c < 0x10FFFF; ++c) {
        UINT16 glyph;
        hr = fontFace->GetGlyphIndices(&c, 1, &glyph);
        if (glyph > 0) {
            ++count;
        }
    }

    SkAutoTArray<UINT32> chars(count);
    count = 0;
    for (UINT32 c = 0; c < 0x10FFFF; ++c) {
        UINT16 glyph;
        hr = fontFace->GetGlyphIndices(&c, 1, &glyph);
        if (glyph > 0) {
            chars[count] = c;
            ++count;
        }
    }

    SkAutoTArray<UINT16> glyph(count);
    fontFace->GetGlyphIndices(chars.get(), count, glyph.get());

    USHORT maxGlyph = 0;
    for (USHORT j = 0; j < count; ++j) {
        if (glyph[j] > maxGlyph) maxGlyph = glyph[j];
    }

    glyphToUnicode->setCount(maxGlyph+1);
    for (USHORT j = 0; j < maxGlyph+1u; ++j) {
        (*glyphToUnicode)[j] = 0;
    }

    //'invert'
    for (USHORT j = 0; j < count; ++j) {
        if (glyph[j] < glyphCount && (*glyphToUnicode)[glyph[j]] == 0) {
            (*glyphToUnicode)[glyph[j]] = chars[j];
        }
    }
}

static bool getWidthAdvance(IDWriteFontFace* fontFace, int gId, int16_t* advance) {
    SkASSERT(advance);

    UINT16 glyphId = gId;
    DWRITE_GLYPH_METRICS gm;
    HRESULT hr = fontFace->GetDesignGlyphMetrics(&glyphId, 1, &gm);

    if (FAILED(hr)) {
        *advance = 0;
        return false;
    }

    *advance = gm.advanceWidth;
    return true;
}

SkAdvancedTypefaceMetrics* DWriteFontTypeface::onGetAdvancedTypefaceMetrics(
        SkAdvancedTypefaceMetrics::PerGlyphInfo perGlyphInfo,
        const uint32_t* glyphIDs,
        uint32_t glyphIDsCount) const {

    SkAdvancedTypefaceMetrics* info = NULL;

    HRESULT hr = S_OK;

    const unsigned glyphCount = fDWriteFontFace->GetGlyphCount();

    DWRITE_FONT_METRICS dwfm;
    fDWriteFontFace->GetMetrics(&dwfm);

    info = new SkAdvancedTypefaceMetrics;
    info->fEmSize = dwfm.designUnitsPerEm;
    info->fMultiMaster = false;
    info->fLastGlyphID = SkToU16(glyphCount - 1);
    info->fStyle = 0;


    SkTScopedComPtr<IDWriteLocalizedStrings> familyNames;
    SkTScopedComPtr<IDWriteLocalizedStrings> faceNames;
    hr = fDWriteFontFamily->GetFamilyNames(&familyNames);
    hr = fDWriteFont->GetFaceNames(&faceNames);

    UINT32 familyNameLength;
    hr = familyNames->GetStringLength(0, &familyNameLength);

    UINT32 faceNameLength;
    hr = faceNames->GetStringLength(0, &faceNameLength);

    UINT32 size = familyNameLength+1+faceNameLength+1;
    SkSMallocWCHAR wFamilyName(size);
    hr = familyNames->GetString(0, wFamilyName.get(), size);
    wFamilyName[familyNameLength] = L' ';
    hr = faceNames->GetString(0, &wFamilyName[familyNameLength+1], size - faceNameLength + 1);

    hr = sk_wchar_to_skstring(wFamilyName.get(), &info->fFontName);

    if (perGlyphInfo & SkAdvancedTypefaceMetrics::kToUnicode_PerGlyphInfo) {
        populate_glyph_to_unicode(fDWriteFontFace.get(), glyphCount, &(info->fGlyphToUnicode));
    }

    DWRITE_FONT_FACE_TYPE fontType = fDWriteFontFace->GetType();
    if (fontType == DWRITE_FONT_FACE_TYPE_TRUETYPE ||
        fontType == DWRITE_FONT_FACE_TYPE_TRUETYPE_COLLECTION) {
        info->fType = SkAdvancedTypefaceMetrics::kTrueType_Font;
    } else {
        info->fType = SkAdvancedTypefaceMetrics::kOther_Font;
        info->fItalicAngle = 0;
        info->fAscent = dwfm.ascent;;
        info->fDescent = dwfm.descent;
        info->fStemV = 0;
        info->fCapHeight = dwfm.capHeight;
        info->fBBox = SkIRect::MakeEmpty();
        return info;
    }

    AutoTDWriteTable<SkOTTableHead> headTable(fDWriteFontFace.get());
    AutoTDWriteTable<SkOTTablePostScript> postTable(fDWriteFontFace.get());
    AutoTDWriteTable<SkOTTableHorizontalHeader> hheaTable(fDWriteFontFace.get());
    AutoTDWriteTable<SkOTTableOS2> os2Table(fDWriteFontFace.get());
    if (!headTable.fExists || !postTable.fExists || !hheaTable.fExists || !os2Table.fExists) {
        info->fItalicAngle = 0;
        info->fAscent = dwfm.ascent;;
        info->fDescent = dwfm.descent;
        info->fStemV = 0;
        info->fCapHeight = dwfm.capHeight;
        info->fBBox = SkIRect::MakeEmpty();
        return info;
    }

    //There exist CJK fonts which set the IsFixedPitch and Monospace bits,
    //but have full width, latin half-width, and half-width kana.
    bool fixedWidth = (postTable->isFixedPitch &&
                      (1 == SkEndian_SwapBE16(hheaTable->numberOfHMetrics)));
    //Monospace
    if (fixedWidth) {
        info->fStyle |= SkAdvancedTypefaceMetrics::kFixedPitch_Style;
    }
    //Italic
    if (os2Table->version.v0.fsSelection.field.Italic) {
        info->fStyle |= SkAdvancedTypefaceMetrics::kItalic_Style;
    }
    //Script
    if (SkPanose::FamilyType::Script == os2Table->version.v0.panose.bFamilyType.value) {
        info->fStyle |= SkAdvancedTypefaceMetrics::kScript_Style;
    //Serif
    } else if (SkPanose::FamilyType::TextAndDisplay == os2Table->version.v0.panose.bFamilyType.value &&
               SkPanose::Data::TextAndDisplay::SerifStyle::Triangle <= os2Table->version.v0.panose.data.textAndDisplay.bSerifStyle.value &&
               SkPanose::Data::TextAndDisplay::SerifStyle::NoFit != os2Table->version.v0.panose.data.textAndDisplay.bSerifStyle.value) {
        info->fStyle |= SkAdvancedTypefaceMetrics::kSerif_Style;
    }

    info->fItalicAngle = SkEndian_SwapBE32(postTable->italicAngle) >> 16;

    info->fAscent = SkToS16(dwfm.ascent);
    info->fDescent = SkToS16(dwfm.descent);
    info->fCapHeight = SkToS16(dwfm.capHeight);

    info->fBBox = SkIRect::MakeLTRB((int32_t)SkEndian_SwapBE16((uint16_t)headTable->xMin),
                                    (int32_t)SkEndian_SwapBE16((uint16_t)headTable->yMax),
                                    (int32_t)SkEndian_SwapBE16((uint16_t)headTable->xMax),
                                    (int32_t)SkEndian_SwapBE16((uint16_t)headTable->yMin));

    //TODO: is this even desired? It seems PDF only wants this value for Type1
    //fonts, and we only get here for TrueType fonts.
    info->fStemV = 0;
    /*
    // Figure out a good guess for StemV - Min width of i, I, !, 1.
    // This probably isn't very good with an italic font.
    int16_t min_width = SHRT_MAX;
    info->fStemV = 0;
    char stem_chars[] = {'i', 'I', '!', '1'};
    for (size_t i = 0; i < SK_ARRAY_COUNT(stem_chars); i++) {
        ABC abcWidths;
        if (GetCharABCWidths(hdc, stem_chars[i], stem_chars[i], &abcWidths)) {
            int16_t width = abcWidths.abcB;
            if (width > 0 && width < min_width) {
                min_width = width;
                info->fStemV = min_width;
            }
        }
    }
    */

    // If Restricted, the font may not be embedded in a document.
    // If not Restricted, the font can be embedded.
    // If PreviewPrint, the embedding is read-only.
    if (os2Table->version.v0.fsType.field.Restricted) {
        info->fType = SkAdvancedTypefaceMetrics::kNotEmbeddable_Font;
    } else if (perGlyphInfo & SkAdvancedTypefaceMetrics::kHAdvance_PerGlyphInfo) {
        if (fixedWidth) {
            appendRange(&info->fGlyphWidths, 0);
            int16_t advance;
            getWidthAdvance(fDWriteFontFace.get(), 1, &advance);
            info->fGlyphWidths->fAdvance.append(1, &advance);
            finishRange(info->fGlyphWidths.get(), 0,
                        SkAdvancedTypefaceMetrics::WidthRange::kDefault);
        } else {
            info->fGlyphWidths.reset(
                getAdvanceData(fDWriteFontFace.get(),
                               glyphCount,
                               glyphIDs,
                               glyphIDsCount,
                               getWidthAdvance));
        }
    }

    return info;
}

///////////////////////////////////////////////////////////////////////////////

SkTypeface* SkFontMgr_DirectWrite::createTypefaceFromDWriteFont(
        IDWriteFontFace* fontFace,
        IDWriteFont* font,
        IDWriteFontFamily* fontFamily) const {
    SkTypeface* face = FindByProcAndRef(FindByDWriteFont, font);
    if (NULL == face) {
        face = DWriteFontTypeface::Create(fFactory.get(), fontFace, font, fontFamily);
        if (face) {
            Add(face, get_style(font), true);
        }
    }
    return face;
}

int SkFontMgr_DirectWrite::onCountFamilies() const {
    return fFontCollection->GetFontFamilyCount();
}

void SkFontMgr_DirectWrite::onGetFamilyName(int index, SkString* familyName) const {
    SkTScopedComPtr<IDWriteFontFamily> fontFamily;
    HRVM(fFontCollection->GetFontFamily(index, &fontFamily), "Could not get requested family.");

    SkTScopedComPtr<IDWriteLocalizedStrings> familyNames;
    HRVM(fontFamily->GetFamilyNames(&familyNames), "Could not get family names.");

    sk_get_locale_string(familyNames.get(), fLocaleName.get(), familyName);
}

SkFontStyleSet* SkFontMgr_DirectWrite::onCreateStyleSet(int index) const {
    SkTScopedComPtr<IDWriteFontFamily> fontFamily;
    HRNM(fFontCollection->GetFontFamily(index, &fontFamily), "Could not get requested family.");

    return SkNEW_ARGS(SkFontStyleSet_DirectWrite, (this, fontFamily.get()));
}

SkFontStyleSet* SkFontMgr_DirectWrite::onMatchFamily(const char familyName[]) const {
    SkSMallocWCHAR dwFamilyName;
    HRN(sk_cstring_to_wchar(familyName, &dwFamilyName));

    UINT32 index;
    BOOL exists;
    HRNM(fFontCollection->FindFamilyName(dwFamilyName.get(), &index, &exists),
            "Failed while finding family by name.");
    if (!exists) {
        return NULL;
    }

    return this->onCreateStyleSet(index);
}

SkTypeface* SkFontMgr_DirectWrite::onMatchFamilyStyle(const char familyName[],
                                                      const SkFontStyle& fontstyle) const {
    SkAutoTUnref<SkFontStyleSet> sset(this->matchFamily(familyName));
    return sset->matchStyle(fontstyle);
}

SkTypeface* SkFontMgr_DirectWrite::onMatchFaceStyle(const SkTypeface* familyMember,
                                                    const SkFontStyle& fontstyle) const {
    SkString familyName;
    SkFontStyleSet_DirectWrite sset(
        this, ((DWriteFontTypeface*)familyMember)->fDWriteFontFamily.get()
    );
    return sset.matchStyle(fontstyle);
}

SkTypeface* SkFontMgr_DirectWrite::onCreateFromStream(SkStream* stream, int ttcIndex) const {
    SkTScopedComPtr<StreamFontFileLoader> fontFileLoader;
    HRN(StreamFontFileLoader::Create(stream, &fontFileLoader));
    HRN(fFactory->RegisterFontFileLoader(fontFileLoader.get()));
    SkAutoIDWriteUnregister<StreamFontFileLoader> autoUnregisterFontFileLoader(
        fFactory.get(), fontFileLoader.get());

    SkTScopedComPtr<StreamFontCollectionLoader> fontCollectionLoader;
    HRN(StreamFontCollectionLoader::Create(fontFileLoader.get(), &fontCollectionLoader));
    HRN(fFactory->RegisterFontCollectionLoader(fontCollectionLoader.get()));
    SkAutoIDWriteUnregister<StreamFontCollectionLoader> autoUnregisterFontCollectionLoader(
        fFactory.get(), fontCollectionLoader.get());

    SkTScopedComPtr<IDWriteFontCollection> fontCollection;
    HRN(fFactory->CreateCustomFontCollection(fontCollectionLoader.get(), NULL, 0, &fontCollection));

    // Find the first non-simulated font which has the given ttc index.
    UINT32 familyCount = fontCollection->GetFontFamilyCount();
    for (UINT32 familyIndex = 0; familyIndex < familyCount; ++familyIndex) {
        SkTScopedComPtr<IDWriteFontFamily> fontFamily;
        HRN(fontCollection->GetFontFamily(familyIndex, &fontFamily));

        UINT32 fontCount = fontFamily->GetFontCount();
        for (UINT32 fontIndex = 0; fontIndex < fontCount; ++fontIndex) {
            SkTScopedComPtr<IDWriteFont> font;
            HRN(fontFamily->GetFont(fontIndex, &font));
            if (font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE) {
                continue;
            }

            SkTScopedComPtr<IDWriteFontFace> fontFace;
            HRN(font->CreateFontFace(&fontFace));

            UINT32 faceIndex = fontFace->GetIndex();
            if (faceIndex == ttcIndex) {
                return DWriteFontTypeface::Create(fFactory.get(),
                                                  fontFace.get(), font.get(), fontFamily.get(),
                                                  autoUnregisterFontFileLoader.detatch(),
                                                  autoUnregisterFontCollectionLoader.detatch());
            }
        }
    }

    return NULL;
}

SkTypeface* SkFontMgr_DirectWrite::onCreateFromData(SkData* data, int ttcIndex) const {
    SkAutoTUnref<SkStream> stream(SkNEW_ARGS(SkMemoryStream, (data)));
    return this->createFromStream(stream, ttcIndex);
}

SkTypeface* SkFontMgr_DirectWrite::onCreateFromFile(const char path[], int ttcIndex) const {
    SkAutoTUnref<SkStream> stream(SkStream::NewFromFile(path));
    return this->createFromStream(stream, ttcIndex);
}

HRESULT SkFontMgr_DirectWrite::getByFamilyName(const WCHAR wideFamilyName[],
                                               IDWriteFontFamily** fontFamily) const {
    UINT32 index;
    BOOL exists;
    HR(fFontCollection->FindFamilyName(wideFamilyName, &index, &exists));

    if (exists) {
        HR(fFontCollection->GetFontFamily(index, fontFamily));
    }
    return S_OK;
}

HRESULT SkFontMgr_DirectWrite::getDefaultFontFamily(IDWriteFontFamily** fontFamily) const {
    NONCLIENTMETRICSW metrics;
    metrics.cbSize = sizeof(metrics);
    if (0 == SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,
                                   sizeof(metrics),
                                   &metrics,
                                   0)) {
        return E_UNEXPECTED;
    }
    HRM(this->getByFamilyName(metrics.lfMessageFont.lfFaceName, fontFamily),
        "Could not create DWrite font family from LOGFONT.");
    return S_OK;
}

SkTypeface* SkFontMgr_DirectWrite::onLegacyCreateTypeface(const char familyName[],
                                                          unsigned styleBits) const {
    SkTScopedComPtr<IDWriteFontFamily> fontFamily;
    if (familyName) {
        SkSMallocWCHAR wideFamilyName;
        if (SUCCEEDED(sk_cstring_to_wchar(familyName, &wideFamilyName))) {
            this->getByFamilyName(wideFamilyName, &fontFamily);
        }
    }

    if (NULL == fontFamily.get()) {
        // No family with given name, try default.
        HRNM(this->getDefaultFontFamily(&fontFamily), "Could not get default font family.");
    }

    if (NULL == fontFamily.get()) {
        // Could not obtain the default font.
        HRNM(fFontCollection->GetFontFamily(0, &fontFamily),
             "Could not get default-default font family.");
    }

    SkTScopedComPtr<IDWriteFont> font;
    DWRITE_FONT_WEIGHT weight = (styleBits & SkTypeface::kBold)
                              ? DWRITE_FONT_WEIGHT_BOLD
                              : DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL;
    DWRITE_FONT_STYLE italic = (styleBits & SkTypeface::kItalic)
                             ? DWRITE_FONT_STYLE_ITALIC
                             : DWRITE_FONT_STYLE_NORMAL;
    HRNM(fontFamily->GetFirstMatchingFont(weight, stretch, italic, &font),
         "Could not get matching font.");

    SkTScopedComPtr<IDWriteFontFace> fontFace;
    HRNM(font->CreateFontFace(&fontFace), "Could not create font face.");

    return this->createTypefaceFromDWriteFont(fontFace.get(), font.get(), fontFamily.get());
}

///////////////////////////////////////////////////////////////////////////////

int SkFontStyleSet_DirectWrite::count() {
    return fFontFamily->GetFontCount();
}

SkTypeface* SkFontStyleSet_DirectWrite::createTypeface(int index) {
    SkTScopedComPtr<IDWriteFont> font;
    HRNM(fFontFamily->GetFont(index, &font), "Could not get font.");

    SkTScopedComPtr<IDWriteFontFace> fontFace;
    HRNM(font->CreateFontFace(&fontFace), "Could not create font face.");

    return fFontMgr->createTypefaceFromDWriteFont(fontFace.get(), font.get(), fFontFamily.get());
}

void SkFontStyleSet_DirectWrite::getStyle(int index, SkFontStyle* fs, SkString* styleName) {
    SkTScopedComPtr<IDWriteFont> font;
    HRVM(fFontFamily->GetFont(index, &font), "Could not get font.");

    if (fs) {
        SkFontStyle::Slant slant;
        switch (font->GetStyle()) {
        case DWRITE_FONT_STYLE_NORMAL:
            slant = SkFontStyle::kUpright_Slant;
            break;
        case DWRITE_FONT_STYLE_OBLIQUE:
        case DWRITE_FONT_STYLE_ITALIC:
            slant = SkFontStyle::kItalic_Slant;
            break;
        default:
            SkASSERT(false);
        }

        int weight = font->GetWeight();
        int width = font->GetStretch();

        *fs = SkFontStyle(weight, width, slant);
    }

    if (styleName) {
        SkTScopedComPtr<IDWriteLocalizedStrings> faceNames;
        if (SUCCEEDED(font->GetFaceNames(&faceNames))) {
            sk_get_locale_string(faceNames.get(), fFontMgr->fLocaleName.get(), styleName);
        }
    }
}

SkTypeface* SkFontStyleSet_DirectWrite::matchStyle(const SkFontStyle& pattern) {
    DWRITE_FONT_STYLE slant;
    switch (pattern.slant()) {
    case SkFontStyle::kUpright_Slant:
        slant = DWRITE_FONT_STYLE_NORMAL;
        break;
    case SkFontStyle::kItalic_Slant:
        slant = DWRITE_FONT_STYLE_ITALIC;
        break;
    default:
        SkASSERT(false);
    }

    DWRITE_FONT_WEIGHT weight = (DWRITE_FONT_WEIGHT)pattern.weight();
    DWRITE_FONT_STRETCH width = (DWRITE_FONT_STRETCH)pattern.width();

    SkTScopedComPtr<IDWriteFont> font;
    // TODO: perhaps use GetMatchingFonts and get the least simulated?
    HRNM(fFontFamily->GetFirstMatchingFont(weight, width, slant, &font),
         "Could not match font in family.");

    SkTScopedComPtr<IDWriteFontFace> fontFace;
    HRNM(font->CreateFontFace(&fontFace), "Could not create font face.");

    return fFontMgr->createTypefaceFromDWriteFont(fontFace.get(), font.get(),
                                                  fFontFamily.get());
}

///////////////////////////////////////////////////////////////////////////////

SkFontMgr* SkFontMgr_New_DirectWrite(IDWriteFactory* factory) {
    if (NULL == factory) {
        factory = sk_get_dwrite_factory();
        if (NULL == factory) {
            return NULL;
        }
    }

    SkTScopedComPtr<IDWriteFontCollection> sysFontCollection;
    HRNM(factory->GetSystemFontCollection(&sysFontCollection, FALSE),
         "Could not get system font collection.");

    WCHAR localeNameStorage[LOCALE_NAME_MAX_LENGTH];
    WCHAR* localeName = NULL;
    int localeNameLen = 0;

    // Dynamically load GetUserDefaultLocaleName function, as it is not available on XP.
    SkGetUserDefaultLocaleNameProc getUserDefaultLocaleNameProc = NULL;
    HRESULT hr = SkGetGetUserDefaultLocaleNameProc(&getUserDefaultLocaleNameProc);
    if (NULL == getUserDefaultLocaleNameProc) {
        SK_TRACEHR(hr, "Could not get GetUserDefaultLocaleName.");
    } else {
        localeNameLen = getUserDefaultLocaleNameProc(localeNameStorage, LOCALE_NAME_MAX_LENGTH);
        if (localeNameLen) {
            localeName = localeNameStorage;
        };
    }

    return SkNEW_ARGS(SkFontMgr_DirectWrite, (factory, sysFontCollection.get(),
                                              localeName, localeNameLen));
}

#include "SkFontMgr_indirect.h"
SkFontMgr* SkFontMgr_New_DirectWriteRenderer(SkRemotableFontMgr* proxy) {
    SkAutoTUnref<SkFontMgr> impl(SkFontMgr_New_DirectWrite());
    if (impl.get() == NULL) {
        return NULL;
    }
    return SkNEW_ARGS(SkFontMgr_Indirect, (impl.get(), proxy));
}
