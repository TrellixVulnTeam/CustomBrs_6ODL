// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYNC_SYNCABLE_MUTABLE_ENTRY_H_
#define SYNC_SYNCABLE_MUTABLE_ENTRY_H_

#include "sync/base/sync_export.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/syncable/entry.h"
#include "sync/syncable/metahandle_set.h"
#include "sync/syncable/model_neutral_mutable_entry.h"

namespace syncer {
class WriteNode;

namespace syncable {

enum Create {
  CREATE
};

class WriteTransaction;

// A mutable meta entry.  Changes get committed to the database when the
// WriteTransaction is destroyed.
class SYNC_EXPORT_PRIVATE MutableEntry : public ModelNeutralMutableEntry {
  void Init(WriteTransaction* trans, ModelType model_type,
            const Id& parent_id, const std::string& name);

 public:
  MutableEntry(WriteTransaction* trans, CreateNewUpdateItem, const Id& id);
  MutableEntry(WriteTransaction* trans, Create, ModelType model_type,
               const Id& parent_id, const std::string& name);
  MutableEntry(WriteTransaction* trans, GetByHandle, int64);
  MutableEntry(WriteTransaction* trans, GetById, const Id&);
  MutableEntry(WriteTransaction* trans, GetByClientTag, const std::string& tag);
  MutableEntry(WriteTransaction* trans, GetByServerTag, const std::string& tag);

  inline WriteTransaction* write_transaction() const {
    return write_transaction_;
  }

  // Model-changing setters.  These setters make user-visible changes that will
  // need to be communicated either to the local model or the sync server.
  void PutLocalExternalId(int64 value);
  void PutMtime(base::Time value);
  void PutCtime(base::Time value);
  void PutParentId(const Id& value);
  void PutIsDir(bool value);
  void PutIsDel(bool value);
  void PutNonUniqueName(const std::string& value);
  void PutSpecifics(const sync_pb::EntitySpecifics& value);
  void PutUniquePosition(const UniquePosition& value);

  // Sets the position of this item, and updates the entry kernels of the
  // adjacent siblings so that list invariants are maintained.  Returns false
  // and fails if |predecessor_id| does not identify a sibling.  Pass the root
  // ID to put the node in first position.
  bool PutPredecessor(const Id& predecessor_id);

  void PutAttachmentMetadata(
      const sync_pb::AttachmentMetadata& attachment_metadata);

  // Update attachment metadata, replace all records matching attachment id's
  // unique id with updated attachment id that contains server info.
  // Set is_in_server for corresponding records.
  void UpdateAttachmentIdWithServerInfo(
      const sync_pb::AttachmentIdProto& updated_attachment_id);

 private:
  // Kind of redundant. We should reduce the number of pointers
  // floating around if at all possible. Could we store this in Directory?
  // Scope: Set on construction, never changed after that.
  WriteTransaction* const write_transaction_;

  DISALLOW_COPY_AND_ASSIGN(MutableEntry);
};

// This function sets only the flags needed to get this entry to sync.
bool MarkForSyncing(syncable::MutableEntry* e);

}  // namespace syncable
}  // namespace syncer

#endif  // SYNC_SYNCABLE_MUTABLE_ENTRY_H_
