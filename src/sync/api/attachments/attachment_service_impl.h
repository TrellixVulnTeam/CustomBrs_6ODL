// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYNC_API_ATTACHMENTS_ATTACHMENT_SERVICE_IMPL_H_
#define SYNC_API_ATTACHMENTS_ATTACHMENT_SERVICE_IMPL_H_

#include "base/memory/weak_ptr.h"
#include "base/threading/non_thread_safe.h"
#include "sync/api/attachments/attachment_service.h"
#include "sync/api/attachments/attachment_service_proxy.h"
#include "sync/api/attachments/attachment_store.h"
#include "sync/api/attachments/attachment_uploader.h"

namespace syncer {

// Implementation of AttachmentService.
class SYNC_EXPORT AttachmentServiceImpl : public AttachmentService,
                                          public base::NonThreadSafe {
 public:
  // |delegate| is optional delegate for AttachmentService to notify about
  // asynchronous events (AttachmentUploaded). Pass NULL if delegate is not
  // provided. AttachmentService doesn't take ownership of delegate, the pointer
  // must be valid throughout AttachmentService lifetime.
  AttachmentServiceImpl(scoped_ptr<AttachmentStore> attachment_store,
                        scoped_ptr<AttachmentUploader> attachment_uploader,
                        Delegate* delegate);
  virtual ~AttachmentServiceImpl();

  // Create an AttachmentServiceImpl suitable for use in tests.
  static scoped_ptr<syncer::AttachmentService> CreateForTest();

  // AttachmentService implementation.
  virtual void GetOrDownloadAttachments(const AttachmentIdList& attachment_ids,
                                        const GetOrDownloadCallback& callback)
      OVERRIDE;
  virtual void DropAttachments(const AttachmentIdList& attachment_ids,
                               const DropCallback& callback) OVERRIDE;
  virtual void StoreAttachments(const AttachmentList& attachments,
                                const StoreCallback& callback) OVERRIDE;
  virtual void OnSyncDataDelete(const SyncData& sync_data) OVERRIDE;
  virtual void OnSyncDataUpdate(const AttachmentIdList& old_attachment_ids,
                                const SyncData& updated_sync_data) OVERRIDE;

 private:
  void ReadDone(const GetOrDownloadCallback& callback,
                const AttachmentStore::Result& result,
                scoped_ptr<AttachmentMap> attachments);
  void DropDone(const DropCallback& callback,
                const AttachmentStore::Result& result);
  void WriteDone(const StoreCallback& callback,
                 const AttachmentStore::Result& result);
  void UploadDone(const AttachmentUploader::UploadResult& result,
                  const AttachmentId& attachment_id);

  const scoped_ptr<AttachmentStore> attachment_store_;
  const scoped_ptr<AttachmentUploader> attachment_uploader_;
  Delegate* delegate_;
  // Must be last data member.
  base::WeakPtrFactory<AttachmentServiceImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(AttachmentServiceImpl);
};

}  // namespace syncer

#endif  // SYNC_API_ATTACHMENTS_ATTACHMENT_SERVICE_IMPL_H_
