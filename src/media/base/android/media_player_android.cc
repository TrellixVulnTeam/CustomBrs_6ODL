// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/android/media_player_android.h"

#include "base/logging.h"
#include "media/base/android/media_player_manager.h"
#include "media/base/media_keys.h"

namespace media {

MediaPlayerAndroid::MediaPlayerAndroid(
    int player_id,
    MediaPlayerManager* manager,
    const RequestMediaResourcesCB& request_media_resources_cb,
    const ReleaseMediaResourcesCB& release_media_resources_cb)
    : request_media_resources_cb_(request_media_resources_cb),
      release_media_resources_cb_(release_media_resources_cb),
      player_id_(player_id),
      manager_(manager) {
}

MediaPlayerAndroid::~MediaPlayerAndroid() {}

GURL MediaPlayerAndroid::GetUrl() {
  return GURL();
}

GURL MediaPlayerAndroid::GetFirstPartyForCookies() {
  return GURL();
}

void MediaPlayerAndroid::SetCdm(MediaKeys* cdm) {
  // Not all players support CDMs. Do nothing by default.
  return;
}

void MediaPlayerAndroid::OnKeyAdded() {
  // Not all players care about the decryption key. Do nothing by default.
  return;
}

}  // namespace media
