/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "Init.h"

#include "EventNames.h"
#include "EventTargetModulesNames.h"
#include "EventTargetNames.h"
#include "EventTypeNames.h"
#include "FetchInitiatorTypeNames.h"
#include "FontFamilyNames.h"
#include "HTMLNames.h"
#include "HTMLTokenizerNames.h"
#include "InputTypeNames.h"
#include "MathMLNames.h"
#include "MediaFeatureNames.h"
#include "MediaTypeNames.h"
#include "SVGNames.h"
#include "XLinkNames.h"
#include "XMLNSNames.h"
#include "XMLNames.h"
#include "core/html/parser/HTMLParserThread.h"
#include "platform/EventTracer.h"
#include "platform/Partitions.h"
#include "platform/PlatformThreadData.h"
#include "platform/heap/Heap.h"
#include "wtf/text/StringStatics.h"

namespace WebCore {

void init()
{
    static bool isInited;
    if (isInited)
        return;
    isInited = true;

    // It would make logical sense to do this and WTF::StringStatics::init() in
    // WTF::initialize() but there are ordering dependencies.
    AtomicString::init();
    HTMLNames::init();
    SVGNames::init();
    XLinkNames::init();
    MathMLNames::init();
    XMLNSNames::init();
    XMLNames::init();

    EventNames::init();
    EventTargetNames::init();
    EventTargetNames::initModules(); // TODO: remove this later http://crbug.com/371581.
    EventTypeNames::init();
    FetchInitiatorTypeNames::init();
    FontFamilyNames::init();
    HTMLTokenizerNames::init();
    InputTypeNames::init();
    MediaFeatureNames::init();
    MediaTypeNames::init();

    WTF::StringStatics::init();
    QualifiedName::init();
    Partitions::init();
    EventTracer::initialize();

    // Ensure that the main thread's thread-local data is initialized before
    // starting any worker threads.
    PlatformThreadData::current();

    StringImpl::freezeStaticStrings();

    // Creates HTMLParserThread::shared, but does not start the thread.
    HTMLParserThread::init();
}

void shutdown()
{
    // Make sure we stop the HTMLParserThread before Platform::current() is cleared.
    HTMLParserThread::shutdown();

    Partitions::shutdown();
}

} // namespace WebCore