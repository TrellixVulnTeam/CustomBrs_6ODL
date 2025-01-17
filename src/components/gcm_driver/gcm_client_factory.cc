// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/gcm_driver/gcm_client_factory.h"

#include "google_apis/gcm/gcm_client_impl.h"

namespace gcm {

scoped_ptr<GCMClient> GCMClientFactory::BuildInstance() {
  return scoped_ptr<GCMClient>(new GCMClientImpl(
      make_scoped_ptr<GCMInternalsBuilder>(new GCMInternalsBuilder())));
}

GCMClientFactory::GCMClientFactory() {
}

GCMClientFactory::~GCMClientFactory() {
}

}  // namespace gcm
