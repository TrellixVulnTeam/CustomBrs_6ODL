// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/cast/transport/transport_audio_sender.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "media/cast/transport/rtp_sender/rtp_sender.h"

namespace media {
namespace cast {
namespace transport {

TransportAudioSender::TransportAudioSender(
    const CastTransportAudioConfig& config,
    base::TickClock* clock,
    const scoped_refptr<base::SingleThreadTaskRunner>& transport_task_runner,
    PacedSender* const paced_packet_sender)
    : rtp_sender_(clock, transport_task_runner, paced_packet_sender),
      encryptor_() {
  initialized_ = rtp_sender_.InitializeAudio(config) &&
      encryptor_.Initialize(config.rtp.config.aes_key,
                            config.rtp.config.aes_iv_mask);
}

TransportAudioSender::~TransportAudioSender() {}

void TransportAudioSender::SendFrame(const EncodedFrame& audio_frame) {
  if (!initialized_) {
    return;
  }
  if (encryptor_.initialized()) {
    EncodedFrame encrypted_frame;
    if (!EncryptAudioFrame(audio_frame, &encrypted_frame)) {
      NOTREACHED();
      return;
    }
    rtp_sender_.SendFrame(encrypted_frame);
  } else {
    rtp_sender_.SendFrame(audio_frame);
  }
}

bool TransportAudioSender::EncryptAudioFrame(
    const EncodedFrame& audio_frame, EncodedFrame* encrypted_frame) {
  if (!initialized_) {
    return false;
  }
  if (!encryptor_.Encrypt(
          audio_frame.frame_id, audio_frame.data, &encrypted_frame->data))
    return false;

  encrypted_frame->dependency = audio_frame.dependency;
  encrypted_frame->frame_id = audio_frame.frame_id;
  encrypted_frame->referenced_frame_id = audio_frame.referenced_frame_id;
  encrypted_frame->rtp_timestamp = audio_frame.rtp_timestamp;
  encrypted_frame->reference_time = audio_frame.reference_time;
  return true;
}

void TransportAudioSender::ResendPackets(
    const MissingFramesAndPacketsMap& missing_frames_and_packets) {
  if (!initialized_) {
    return;
  }
  rtp_sender_.ResendPackets(missing_frames_and_packets);
}

}  // namespace transport
}  // namespace cast
}  // namespace media
