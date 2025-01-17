/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * A common place to put the jsoncpp library includes, as opposed to littering
 * the pragmas repeatedly through our code.
 */
#ifndef SkJSONCPP_DEFINED
#define SkJSONCPP_DEFINED

#ifdef SK_BUILD_JSON_WRITER

#ifdef SK_BUILD_FOR_WIN
    // json includes xlocale which generates warning 4530 because we're
    // compiling without exceptions;
    // see https://code.google.com/p/skia/issues/detail?id=1067
    #pragma warning(push)
    #pragma warning(disable : 4530)
#endif
#include "json/reader.h"
#include "json/value.h"
#ifdef SK_BUILD_FOR_WIN
    #pragma warning(pop)
#endif

#endif // SK_BUILD_JSON_WRITER

#endif // SkJSONCPP_DEFINED
