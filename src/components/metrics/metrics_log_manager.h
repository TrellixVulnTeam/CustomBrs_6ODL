// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_METRICS_METRICS_LOG_MANAGER_H_
#define COMPONENTS_METRICS_METRICS_LOG_MANAGER_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "components/metrics/metrics_log_base.h"
#include "components/metrics/persisted_logs.h"

namespace metrics {

// Manages all the log objects used by a MetricsService implementation. Keeps
// track of both an in progress log and a log that is staged for uploading as
// text, as well as saving logs to, and loading logs from, persistent storage.
class MetricsLogManager {
 public:
  typedef MetricsLogBase::LogType LogType;

  // The metrics log manager will persist it's unsent logs by storing them in
  // |local_state|, and will not persist ongoing logs over
  // |max_ongoing_log_size|.
  MetricsLogManager(PrefService* local_state, size_t max_ongoing_log_size);
  ~MetricsLogManager();

  // Takes ownership of |log| and makes it the current_log. This should only be
  // called if there is not a current log.
  void BeginLoggingWithLog(MetricsLogBase* log);

  // Returns the in-progress log.
  MetricsLogBase* current_log() { return current_log_.get(); }

  // Closes current_log(), compresses it, and stores the compressed log for
  // later, leaving current_log() NULL.
  void FinishCurrentLog();

  // Returns true if there are any logs waiting to be uploaded.
  bool has_unsent_logs() const {
    return initial_log_queue_.size() || ongoing_log_queue_.size();
  }

  // Populates staged_log_text() with the next stored log to send.
  // Should only be called if has_unsent_logs() is true.
  void StageNextLogForUpload();

  // Returns true if there is a log that needs to be, or is being, uploaded.
  bool has_staged_log() const {
    return initial_log_queue_.has_staged_log() ||
        ongoing_log_queue_.has_staged_log();
  }

  // The text of the staged log, as a serialized protobuf.
  // Will trigger a DCHECK if there is no staged log.
  const std::string& staged_log() const {
    return initial_log_queue_.has_staged_log() ?
        initial_log_queue_.staged_log() : ongoing_log_queue_.staged_log();
  }

  // The SHA1 hash of the staged log.
  // Will trigger a DCHECK if there is no staged log.
  const std::string& staged_log_hash() const {
    return initial_log_queue_.has_staged_log() ?
        initial_log_queue_.staged_log_hash() :
        ongoing_log_queue_.staged_log_hash();
  }

  // Discards the staged log.
  void DiscardStagedLog();

  // Closes and discards |current_log|.
  void DiscardCurrentLog();

  // Sets current_log to NULL, but saves the current log for future use with
  // ResumePausedLog(). Only one log may be paused at a time.
  // TODO(stuartmorgan): Pause/resume support is really a workaround for a
  // design issue in initial log writing; that should be fixed, and pause/resume
  // removed.
  void PauseCurrentLog();

  // Restores the previously paused log (if any) to current_log().
  // This should only be called if there is not a current log.
  void ResumePausedLog();

  // Saves the staged log, then clears staged_log().
  // If |store_type| is PROVISIONAL_STORE, it can be dropped from storage with
  // a later call to DiscardLastProvisionalStore (if it hasn't already been
  // staged again).
  // This is intended to be used when logs are being saved while an upload is in
  // progress, in case the upload later succeeds.
  // This can only be called if has_staged_log() is true.
  void StoreStagedLogAsUnsent(metrics::PersistedLogs::StoreType store_type);

  // Discards the last log stored with StoreStagedLogAsUnsent with |store_type|
  // set to PROVISIONAL_STORE, as long as it hasn't already been re-staged. If
  // the log is no longer present, this is a no-op.
  void DiscardLastProvisionalStore();

  // Saves any unsent logs to persistent storage.
  void PersistUnsentLogs();

  // Loads any unsent logs from persistent storage.
  void LoadPersistedUnsentLogs();

 private:
  // Saves |log| as the given type.
  // NOTE: This clears the contents of |log| (to avoid an expensive copy),
  // so the log should be discarded after this call.
  void StoreLog(std::string* log, LogType log_type);

  // Tracks whether unsent logs (if any) have been loaded from the serializer.
  bool unsent_logs_loaded_;

  // The log that we are still appending to.
  scoped_ptr<MetricsLogBase> current_log_;

  // A paused, previously-current log.
  scoped_ptr<MetricsLogBase> paused_log_;

  // Logs that have not yet been sent.
  metrics::PersistedLogs initial_log_queue_;
  metrics::PersistedLogs ongoing_log_queue_;

  DISALLOW_COPY_AND_ASSIGN(MetricsLogManager);
};

}  // namespace metrics

#endif  // COMPONENTS_METRICS_METRICS_LOG_MANAGER_H_
