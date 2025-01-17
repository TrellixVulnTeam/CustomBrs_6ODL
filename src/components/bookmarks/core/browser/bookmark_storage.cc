// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/bookmarks/core/browser/bookmark_storage.h"

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/file_util.h"
#include "base/json/json_file_value_serializer.h"
#include "base/json/json_string_value_serializer.h"
#include "base/metrics/histogram.h"
#include "base/sequenced_task_runner.h"
#include "base/time/time.h"
#include "components/bookmarks/core/browser/bookmark_codec.h"
#include "components/bookmarks/core/browser/bookmark_index.h"
#include "components/bookmarks/core/browser/bookmark_model.h"
#include "components/bookmarks/core/common/bookmark_constants.h"
#include "components/startup_metric_utils/startup_metric_utils.h"

using base::TimeTicks;

namespace {

// Extension used for backup files (copy of main file created during startup).
const base::FilePath::CharType kBackupExtension[] = FILE_PATH_LITERAL("bak");

// How often we save.
const int kSaveDelayMS = 2500;

void BackupCallback(const base::FilePath& path) {
  base::FilePath backup_path = path.ReplaceExtension(kBackupExtension);
  base::CopyFile(path, backup_path);
}

// Adds node to the model's index, recursing through all children as well.
void AddBookmarksToIndex(BookmarkLoadDetails* details,
                         BookmarkNode* node) {
  if (node->is_url()) {
    if (node->url().is_valid())
      details->index()->Add(node);
  } else {
    for (int i = 0; i < node->child_count(); ++i)
      AddBookmarksToIndex(details, node->GetChild(i));
  }
}

void LoadCallback(const base::FilePath& path,
                  BookmarkStorage* storage,
                  BookmarkLoadDetails* details,
                  base::SequencedTaskRunner* task_runner) {
  startup_metric_utils::ScopedSlowStartupUMA
      scoped_timer("Startup.SlowStartupBookmarksLoad");
  bool bookmark_file_exists = base::PathExists(path);
  if (bookmark_file_exists) {
    JSONFileValueSerializer serializer(path);
    scoped_ptr<base::Value> root(serializer.Deserialize(NULL, NULL));

    if (root.get()) {
      // Building the index can take a while, so we do it on the background
      // thread.
      int64 max_node_id = 0;
      BookmarkCodec codec;
      TimeTicks start_time = TimeTicks::Now();
      codec.Decode(details->bb_node(), details->other_folder_node(),
                   details->mobile_folder_node(), &max_node_id, *root.get());
      details->set_max_id(std::max(max_node_id, details->max_id()));
      details->set_computed_checksum(codec.computed_checksum());
      details->set_stored_checksum(codec.stored_checksum());
      details->set_ids_reassigned(codec.ids_reassigned());
      details->set_model_meta_info_map(codec.model_meta_info_map());
      details->set_model_sync_transaction_version(
          codec.model_sync_transaction_version());
      UMA_HISTOGRAM_TIMES("Bookmarks.DecodeTime",
                          TimeTicks::Now() - start_time);

      start_time = TimeTicks::Now();
      AddBookmarksToIndex(details, details->bb_node());
      AddBookmarksToIndex(details, details->other_folder_node());
      AddBookmarksToIndex(details, details->mobile_folder_node());
      UMA_HISTOGRAM_TIMES("Bookmarks.CreateBookmarkIndexTime",
                          TimeTicks::Now() - start_time);
    }
  }

  task_runner->PostTask(FROM_HERE,
                        base::Bind(&BookmarkStorage::OnLoadFinished, storage));
}

}  // namespace

// BookmarkLoadDetails ---------------------------------------------------------

BookmarkLoadDetails::BookmarkLoadDetails(
    BookmarkPermanentNode* bb_node,
    BookmarkPermanentNode* other_folder_node,
    BookmarkPermanentNode* mobile_folder_node,
    BookmarkIndex* index,
    int64 max_id)
    : bb_node_(bb_node),
      other_folder_node_(other_folder_node),
      mobile_folder_node_(mobile_folder_node),
      index_(index),
      model_sync_transaction_version_(
          BookmarkNode::kInvalidSyncTransactionVersion),
      max_id_(max_id),
      ids_reassigned_(false) {
}

BookmarkLoadDetails::~BookmarkLoadDetails() {
}

// BookmarkStorage -------------------------------------------------------------

BookmarkStorage::BookmarkStorage(
    BookmarkModel* model,
    const base::FilePath& profile_path,
    base::SequencedTaskRunner* sequenced_task_runner)
    : model_(model),
      writer_(profile_path.Append(bookmarks::kBookmarksFileName),
              sequenced_task_runner) {
  sequenced_task_runner_ = sequenced_task_runner;
  writer_.set_commit_interval(base::TimeDelta::FromMilliseconds(kSaveDelayMS));
  sequenced_task_runner_->PostTask(FROM_HERE,
                                   base::Bind(&BackupCallback, writer_.path()));
}

BookmarkStorage::~BookmarkStorage() {
  if (writer_.HasPendingWrite())
    writer_.DoScheduledWrite();
}

void BookmarkStorage::LoadBookmarks(
    scoped_ptr<BookmarkLoadDetails> details,
    const scoped_refptr<base::SequencedTaskRunner>& task_runner) {
  DCHECK(!details_.get());
  DCHECK(details);
  details_ = details.Pass();
  sequenced_task_runner_->PostTask(FROM_HERE,
                                   base::Bind(&LoadCallback,
                                              writer_.path(),
                                              make_scoped_refptr(this),
                                              details_.get(),
                                              task_runner));
}

void BookmarkStorage::ScheduleSave() {
  writer_.ScheduleWrite(this);
}

void BookmarkStorage::BookmarkModelDeleted() {
  // We need to save now as otherwise by the time SaveNow is invoked
  // the model is gone.
  if (writer_.HasPendingWrite())
    SaveNow();
  model_ = NULL;
}

bool BookmarkStorage::SerializeData(std::string* output) {
  BookmarkCodec codec;
  scoped_ptr<base::Value> value(codec.Encode(model_));
  JSONStringValueSerializer serializer(output);
  serializer.set_pretty_print(true);
  return serializer.Serialize(*(value.get()));
}

void BookmarkStorage::OnLoadFinished() {
  if (!model_)
    return;

  model_->DoneLoading(details_.Pass());
}

bool BookmarkStorage::SaveNow() {
  if (!model_ || !model_->loaded()) {
    // We should only get here if we have a valid model and it's finished
    // loading.
    NOTREACHED();
    return false;
  }

  std::string data;
  if (!SerializeData(&data))
    return false;
  writer_.WriteNow(data);
  return true;
}
