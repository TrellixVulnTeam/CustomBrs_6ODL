// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/engine/directory_commit_contribution.h"

#include "base/message_loop/message_loop.h"
#include "sync/sessions/status_controller.h"
#include "sync/syncable/entry.h"
#include "sync/syncable/mutable_entry.h"
#include "sync/syncable/syncable_read_transaction.h"
#include "sync/syncable/syncable_write_transaction.h"
#include "sync/test/engine/test_directory_setter_upper.h"
#include "sync/test/engine/test_id_factory.h"
#include "sync/test/engine/test_syncable_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

class DirectoryCommitContributionTest : public ::testing::Test {
 public:
  virtual void SetUp() OVERRIDE {
    dir_maker_.SetUp();

    syncable::WriteTransaction trans(FROM_HERE, syncable::UNITTEST, dir());
    CreateTypeRoot(&trans, dir(), PREFERENCES);
    CreateTypeRoot(&trans, dir(), EXTENSIONS);
  }

  virtual void TearDown() OVERRIDE {
    dir_maker_.TearDown();
  }

 protected:
  int64 CreateUnsyncedItem(syncable::WriteTransaction* trans,
                           ModelType type,
                           const std::string& tag) {
    syncable::Entry parent_entry(
        trans,
        syncable::GET_BY_SERVER_TAG,
        ModelTypeToRootTag(type));
    syncable::MutableEntry entry(
        trans,
        syncable::CREATE,
        type,
        parent_entry.GetId(),
        tag);
    entry.PutIsUnsynced(true);
    return entry.GetMetahandle();
  }

  void CreateSuccessfulCommitResponse(
      const sync_pb::SyncEntity& entity,
      sync_pb::CommitResponse::EntryResponse* response) {
    response->set_response_type(sync_pb::CommitResponse::SUCCESS);
    response->set_non_unique_name(entity.name());
    response->set_version(entity.version() + 1);
    response->set_parent_id_string(entity.parent_id_string());

    if (entity.id_string()[0] == '-')  // Look for the - in 'c-1234' style IDs.
      response->set_id_string(id_factory_.NewServerId().GetServerId());
    else
      response->set_id_string(entity.id_string());
  }

  syncable::Directory* dir() {
    return dir_maker_.directory();
  }

  TestIdFactory id_factory_;

  // Used in construction of DirectoryTypeDebugInfoEmitters.
  ObserverList<TypeDebugInfoObserver> type_observers_;

 private:
  base::MessageLoop loop_;  // Neeed to initialize the directory.
  TestDirectorySetterUpper dir_maker_;
};

// Verify that the DirectoryCommitContribution contains only entries of its
// specified type.
TEST_F(DirectoryCommitContributionTest, GatherByTypes) {
  int64 pref1;
  {
    syncable::WriteTransaction trans(FROM_HERE, syncable::UNITTEST, dir());
    pref1 = CreateUnsyncedItem(&trans, PREFERENCES, "pref1");
    CreateUnsyncedItem(&trans, PREFERENCES, "pref2");
    CreateUnsyncedItem(&trans, EXTENSIONS, "extension1");
  }

  DirectoryTypeDebugInfoEmitter emitter(PREFERENCES, &type_observers_);
  scoped_ptr<DirectoryCommitContribution> cc(
      DirectoryCommitContribution::Build(dir(), PREFERENCES, 5, &emitter));
  ASSERT_EQ(2U, cc->GetNumEntries());

  const std::vector<int64>& metahandles = cc->metahandles_;
  EXPECT_TRUE(std::find(metahandles.begin(), metahandles.end(), pref1) !=
              metahandles.end());
  EXPECT_TRUE(std::find(metahandles.begin(), metahandles.end(), pref1) !=
              metahandles.end());

  cc->CleanUp();
}

// Verify that the DirectoryCommitContributionTest builder function
// truncates if necessary.
TEST_F(DirectoryCommitContributionTest, GatherAndTruncate) {
  int64 pref1;
  int64 pref2;
  {
    syncable::WriteTransaction trans(FROM_HERE, syncable::UNITTEST, dir());
    pref1 = CreateUnsyncedItem(&trans, PREFERENCES, "pref1");
    pref2 = CreateUnsyncedItem(&trans, PREFERENCES, "pref2");
    CreateUnsyncedItem(&trans, EXTENSIONS, "extension1");
  }

  DirectoryTypeDebugInfoEmitter emitter(PREFERENCES, &type_observers_);
  scoped_ptr<DirectoryCommitContribution> cc(
      DirectoryCommitContribution::Build(dir(), PREFERENCES, 1, &emitter));
  ASSERT_EQ(1U, cc->GetNumEntries());

  int64 only_metahandle = cc->metahandles_[0];
  EXPECT_TRUE(only_metahandle == pref1 || only_metahandle == pref2);

  cc->CleanUp();
}

// Sanity check for building commits from DirectoryCommitContributions.
// This test makes two CommitContribution objects of different types and uses
// them to initialize a commit message.  Then it checks that the contents of the
// commit message match those of the directory they came from.
TEST_F(DirectoryCommitContributionTest, PrepareCommit) {
  {
    syncable::WriteTransaction trans(FROM_HERE, syncable::UNITTEST, dir());
    CreateUnsyncedItem(&trans, PREFERENCES, "pref1");
    CreateUnsyncedItem(&trans, PREFERENCES, "pref2");
    CreateUnsyncedItem(&trans, EXTENSIONS, "extension1");
  }

  DirectoryTypeDebugInfoEmitter emitter1(PREFERENCES, &type_observers_);
  DirectoryTypeDebugInfoEmitter emitter2(EXTENSIONS, &type_observers_);
  scoped_ptr<DirectoryCommitContribution> pref_cc(
      DirectoryCommitContribution::Build(dir(), PREFERENCES, 25, &emitter1));
  scoped_ptr<DirectoryCommitContribution> ext_cc(
      DirectoryCommitContribution::Build(dir(), EXTENSIONS, 25, &emitter2));

  sync_pb::ClientToServerMessage message;
  pref_cc->AddToCommitMessage(&message);
  ext_cc->AddToCommitMessage(&message);

  const sync_pb::CommitMessage& commit_message = message.commit();

  std::set<syncable::Id> ids_for_commit;
  ASSERT_EQ(3, commit_message.entries_size());
  for (int i = 0; i < commit_message.entries_size(); ++i) {
    const sync_pb::SyncEntity& entity = commit_message.entries(i);
    // The entities in this test have client-style IDs since they've never been
    // committed before, so we must use CreateFromClientString to re-create them
    // from the commit message.
    ids_for_commit.insert(syncable::Id::CreateFromClientString(
            entity.id_string()));
  }

  ASSERT_EQ(3U, ids_for_commit.size());
  {
    syncable::ReadTransaction trans(FROM_HERE, dir());
    for (std::set<syncable::Id>::iterator it = ids_for_commit.begin();
         it != ids_for_commit.end(); ++it) {
      SCOPED_TRACE(it->value());
      syncable::Entry entry(&trans, syncable::GET_BY_ID, *it);
      ASSERT_TRUE(entry.good());
      EXPECT_TRUE(entry.GetSyncing());
    }
  }

  pref_cc->CleanUp();
  ext_cc->CleanUp();
}

// Creates some unsynced items, pretends to commit them, and hands back a
// specially crafted response to the syncer in order to test commit response
// processing.  The response simulates a succesful commit scenario.
TEST_F(DirectoryCommitContributionTest, ProcessCommitResponse) {
  int64 pref1_handle;
  int64 pref2_handle;
  int64 ext1_handle;
  {
    syncable::WriteTransaction trans(FROM_HERE, syncable::UNITTEST, dir());
    pref1_handle = CreateUnsyncedItem(&trans, PREFERENCES, "pref1");
    pref2_handle = CreateUnsyncedItem(&trans, PREFERENCES, "pref2");
    ext1_handle = CreateUnsyncedItem(&trans, EXTENSIONS, "extension1");
  }

  DirectoryTypeDebugInfoEmitter emitter1(PREFERENCES, &type_observers_);
  DirectoryTypeDebugInfoEmitter emitter2(EXTENSIONS, &type_observers_);
  scoped_ptr<DirectoryCommitContribution> pref_cc(
      DirectoryCommitContribution::Build(dir(), PREFERENCES, 25, &emitter1));
  scoped_ptr<DirectoryCommitContribution> ext_cc(
      DirectoryCommitContribution::Build(dir(), EXTENSIONS, 25, &emitter2));

  sync_pb::ClientToServerMessage message;
  pref_cc->AddToCommitMessage(&message);
  ext_cc->AddToCommitMessage(&message);

  const sync_pb::CommitMessage& commit_message = message.commit();
  ASSERT_EQ(3, commit_message.entries_size());

  sync_pb::ClientToServerResponse response;
  for (int i = 0; i < commit_message.entries_size(); ++i) {
    sync_pb::SyncEntity entity = commit_message.entries(i);
    sync_pb::CommitResponse_EntryResponse* entry_response =
        response.mutable_commit()->add_entryresponse();
    CreateSuccessfulCommitResponse(entity, entry_response);
  }

  sessions::StatusController status;

  // Process these in reverse order.  Just because we can.
  ext_cc->ProcessCommitResponse(response, &status);
  pref_cc->ProcessCommitResponse(response, &status);

  {
    syncable::ReadTransaction trans(FROM_HERE, dir());
    syncable::Entry p1(&trans, syncable::GET_BY_HANDLE, pref1_handle);
    EXPECT_TRUE(p1.GetId().ServerKnows());
    EXPECT_FALSE(p1.GetSyncing());
    EXPECT_LT(0, p1.GetServerVersion());

    syncable::Entry p2(&trans, syncable::GET_BY_HANDLE, pref2_handle);
    EXPECT_TRUE(p2.GetId().ServerKnows());
    EXPECT_FALSE(p2.GetSyncing());
    EXPECT_LT(0, p2.GetServerVersion());

    syncable::Entry e1(&trans, syncable::GET_BY_HANDLE, ext1_handle);
    EXPECT_TRUE(e1.GetId().ServerKnows());
    EXPECT_FALSE(e1.GetSyncing());
    EXPECT_LT(0, e1.GetServerVersion());
  }

  pref_cc->CleanUp();
  ext_cc->CleanUp();
}

}  // namespace syncer
