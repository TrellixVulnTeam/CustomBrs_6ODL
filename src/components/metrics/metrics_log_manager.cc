// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/metrics/metrics_log_manager.h"

#include <algorithm>

#include "base/metrics/histogram.h"
#include "base/strings/string_util.h"
#include "base/timer/elapsed_timer.h"
#include "components/metrics/metrics_log_base.h"
#include "components/metrics/metrics_pref_names.h"

namespace metrics {

namespace {

// The number of "initial" logs to save, and hope to send during a future Chrome
// session.  Initial logs contain crash stats, and are pretty small.
const size_t kInitialLogsPersistLimit = 20;

// The number of ongoing logs to save persistently, and hope to
// send during a this or future sessions.  Note that each log may be pretty
// large, as presumably the related "initial" log wasn't sent (probably nothing
// was, as the user was probably off-line).  As a result, the log probably kept
// accumulating while the "initial" log was stalled, and couldn't be sent.  As a
// result, we don't want to save too many of these mega-logs.
// A "standard shutdown" will create a small log, including just the data that
// was not yet been transmitted, and that is normal (to have exactly one
// ongoing_log_ at startup).
const size_t kOngoingLogsPersistLimit = 8;

// The number of bytes each of initial and ongoing logs that must be stored.
// This ensures that a reasonable amount of history will be stored even if there
// is a long series of very small logs.
const size_t kStorageByteLimitPerLogType = 300000;

}  // namespace

MetricsLogManager::MetricsLogManager(PrefService* local_state,
                                     size_t max_ongoing_log_size)
    : unsent_logs_loaded_(false),
      initial_log_queue_(local_state,
                         prefs::kMetricsInitialLogs,
                         kInitialLogsPersistLimit,
                         kStorageByteLimitPerLogType,
                         0),
      ongoing_log_queue_(local_state,
                         prefs::kMetricsOngoingLogs,
                         kOngoingLogsPersistLimit,
                         kStorageByteLimitPerLogType,
                         max_ongoing_log_size) {}

MetricsLogManager::~MetricsLogManager() {}

void MetricsLogManager::BeginLoggingWithLog(MetricsLogBase* log) {
  DCHECK(!current_log_.get());
  current_log_.reset(log);
}

void MetricsLogManager::FinishCurrentLog() {
  DCHECK(current_log_.get());
  current_log_->CloseLog();
  std::string log_text;
  current_log_->GetEncodedLog(&log_text);
  if (!log_text.empty())
    StoreLog(&log_text, current_log_->log_type());
  current_log_.reset();
}

void MetricsLogManager::StageNextLogForUpload() {
  DCHECK(!has_staged_log());
  if (!initial_log_queue_.empty())
    initial_log_queue_.StageLog();
  else
    ongoing_log_queue_.StageLog();
}

void MetricsLogManager::DiscardStagedLog() {
  DCHECK(has_staged_log());
  if (initial_log_queue_.has_staged_log())
    initial_log_queue_.DiscardStagedLog();
  else
    ongoing_log_queue_.DiscardStagedLog();
  DCHECK(!has_staged_log());
}

void MetricsLogManager::DiscardCurrentLog() {
  current_log_->CloseLog();
  current_log_.reset();
}

void MetricsLogManager::PauseCurrentLog() {
  DCHECK(!paused_log_.get());
  paused_log_.reset(current_log_.release());
}

void MetricsLogManager::ResumePausedLog() {
  DCHECK(!current_log_.get());
  current_log_.reset(paused_log_.release());
}

void MetricsLogManager::StoreLog(std::string* log, LogType log_type) {
  DCHECK_NE(MetricsLogBase::NO_LOG, log_type);
  metrics::PersistedLogs* destination_queue =
      (log_type == MetricsLogBase::INITIAL_STABILITY_LOG) ?
      &initial_log_queue_ : &ongoing_log_queue_;

  destination_queue->StoreLog(log);
}

void MetricsLogManager::StoreStagedLogAsUnsent(
    metrics::PersistedLogs::StoreType store_type) {
  DCHECK(has_staged_log());
  if (initial_log_queue_.has_staged_log())
    initial_log_queue_.StoreStagedLogAsUnsent(store_type);
  else
    ongoing_log_queue_.StoreStagedLogAsUnsent(store_type);
}

void MetricsLogManager::DiscardLastProvisionalStore() {
  // We have at most one provisional store, (since at most one log is being
  // uploaded at a time), so at least one of these will be a no-op.
  initial_log_queue_.DiscardLastProvisionalStore();
  ongoing_log_queue_.DiscardLastProvisionalStore();
}

void MetricsLogManager::PersistUnsentLogs() {
  DCHECK(unsent_logs_loaded_);
  if (!unsent_logs_loaded_)
    return;

  base::ElapsedTimer timer;
  initial_log_queue_.SerializeLogs();
  ongoing_log_queue_.SerializeLogs();
  UMA_HISTOGRAM_TIMES("UMA.StoreLogsTime", timer.Elapsed());
}

void MetricsLogManager::LoadPersistedUnsentLogs() {
  base::ElapsedTimer timer;
  initial_log_queue_.DeserializeLogs();
  ongoing_log_queue_.DeserializeLogs();
  UMA_HISTOGRAM_TIMES("UMA.LoadLogsTime", timer.Elapsed());

  unsent_logs_loaded_ = true;
}

} // namespace metrics
