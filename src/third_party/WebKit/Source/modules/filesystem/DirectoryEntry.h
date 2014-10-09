/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
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

#ifndef DirectoryEntry_h
#define DirectoryEntry_h

#include "modules/filesystem/Entry.h"
#include "modules/filesystem/FileSystemFlags.h"
#include "platform/heap/Handle.h"
#include "wtf/RefCounted.h"
#include "wtf/text/WTFString.h"

namespace WebCore {

class DOMFileSystemBase;
class DirectoryReader;
class EntryCallback;
class ErrorCallback;
class VoidCallback;

class DirectoryEntry FINAL : public Entry {
public:
    static PassRefPtrWillBeRawPtr<DirectoryEntry> create(PassRefPtrWillBeRawPtr<DOMFileSystemBase> fileSystem, const String& fullPath)
    {
        return adoptRefWillBeNoop(new DirectoryEntry(fileSystem, fullPath));
    }
    virtual bool isDirectory() const OVERRIDE { return true; }

    PassRefPtrWillBeRawPtr<DirectoryReader> createReader();
    void getFile(const String& path, const Dictionary&, PassOwnPtr<EntryCallback> = nullptr, PassOwnPtr<ErrorCallback> = nullptr);
    void getDirectory(const String& path, const Dictionary&, PassOwnPtr<EntryCallback> = nullptr, PassOwnPtr<ErrorCallback> = nullptr);
    void removeRecursively(PassOwnPtr<VoidCallback> successCallback = nullptr, PassOwnPtr<ErrorCallback> = nullptr) const;

    virtual void trace(Visitor*) OVERRIDE;

private:
    DirectoryEntry(PassRefPtrWillBeRawPtr<DOMFileSystemBase>, const String& fullPath);
};

DEFINE_TYPE_CASTS(DirectoryEntry, Entry, entry, entry->isDirectory(), entry.isDirectory());

} // namespace

#endif // DirectoryEntry_h