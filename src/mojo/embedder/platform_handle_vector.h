// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EMBEDDER_PLATFORM_HANDLE_VECTOR_H_
#define MOJO_EMBEDDER_PLATFORM_HANDLE_VECTOR_H_

#include <vector>

#include "base/memory/scoped_ptr.h"
#include "mojo/embedder/platform_handle.h"
#include "mojo/system/system_impl_export.h"

namespace mojo {
namespace embedder {

typedef std::vector<PlatformHandle> PlatformHandleVector;

MOJO_SYSTEM_IMPL_EXPORT void CloseAllHandles(
    PlatformHandleVector* platform_handles);

// A deleter (for use with |scoped_ptr|) which closes all handles and then
// |delete|s the |PlatformHandleVector|.
struct MOJO_SYSTEM_IMPL_EXPORT PlatformHandleVectorDeleter {
  void operator()(PlatformHandleVector* platform_handles) const {
    CloseAllHandles(platform_handles);
    delete platform_handles;
  }
};

typedef scoped_ptr<PlatformHandleVector, PlatformHandleVectorDeleter>
    ScopedPlatformHandleVectorPtr;

}  // namespace embedder
}  // namespace mojo

#endif  // MOJO_EMBEDDER_PLATFORM_HANDLE_VECTOR_H_
