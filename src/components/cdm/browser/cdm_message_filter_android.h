// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_CDM_BROWSER_CDM_MESSAGE_FILTER_ANDROID_H_
#define COMPONENTS_CDM_BROWSER_CDM_MESSAGE_FILTER_ANDROID_H_

#include "base/basictypes.h"
#include "content/public/browser/browser_message_filter.h"

struct SupportedKeySystemRequest;
struct SupportedKeySystemResponse;

namespace cdm {

// Message filter for EME on android. It is responsible for getting the
// SupportedKeySystems information and passing it back to renderer.
class CdmMessageFilterAndroid
    : public content::BrowserMessageFilter {
 public:
  CdmMessageFilterAndroid();

 private:
  virtual ~CdmMessageFilterAndroid();

  // BrowserMessageFilter implementation.
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;
  virtual void OverrideThreadForMessage(
      const IPC::Message& message,
      content::BrowserThread::ID* thread) OVERRIDE;

  // Retrieve the supported key systems.
  void OnGetSupportedKeySystems(
      const SupportedKeySystemRequest& request,
      SupportedKeySystemResponse* response);

  DISALLOW_COPY_AND_ASSIGN(CdmMessageFilterAndroid);
};

}  // namespace cdm

#endif  // COMPONENTS_CDM_BROWSER_CDM_MESSAGE_FILTER_ANDROID_H_
