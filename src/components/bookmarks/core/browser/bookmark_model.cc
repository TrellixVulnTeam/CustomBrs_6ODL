// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/bookmarks/core/browser/bookmark_model.h"

#include <algorithm>
#include <functional>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/i18n/string_compare.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/strings/string_util.h"
#include "components/bookmarks/core/browser/bookmark_expanded_state_tracker.h"
#include "components/bookmarks/core/browser/bookmark_index.h"
#include "components/bookmarks/core/browser/bookmark_match.h"
#include "components/bookmarks/core/browser/bookmark_model_observer.h"
#include "components/bookmarks/core/browser/bookmark_node_data.h"
#include "components/bookmarks/core/browser/bookmark_storage.h"
#include "components/bookmarks/core/browser/bookmark_utils.h"
#include "components/favicon_base/favicon_types.h"
#include "grit/component_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/favicon_size.h"

using base::Time;

namespace {

// Helper to get a mutable bookmark node.
BookmarkNode* AsMutable(const BookmarkNode* node) {
  return const_cast<BookmarkNode*>(node);
}

// Helper to get a mutable permanent bookmark node.
BookmarkPermanentNode* AsMutable(const BookmarkPermanentNode* node) {
  return const_cast<BookmarkPermanentNode*>(node);
}

// Comparator used when sorting permanent nodes. Nodes that are initially
// visible are sorted before nodes that are initially hidden.
class VisibilityComparator
    : public std::binary_function<const BookmarkPermanentNode*,
                                  const BookmarkPermanentNode*,
                                  bool> {
 public:
  explicit VisibilityComparator(BookmarkClient* client) : client_(client) {}

  // Returns true if |n1| preceeds |n2|.
  bool operator()(const BookmarkPermanentNode* n1,
                  const BookmarkPermanentNode* n2) {
    bool n1_visible = client_->IsPermanentNodeVisible(n1->type());
    bool n2_visible = client_->IsPermanentNodeVisible(n2->type());
    return n1_visible != n2_visible && n1_visible;
  }

 private:
  BookmarkClient* client_;
};

// Comparator used when sorting bookmarks. Folders are sorted first, then
// bookmarks.
class SortComparator : public std::binary_function<const BookmarkNode*,
                                                   const BookmarkNode*,
                                                   bool> {
 public:
  explicit SortComparator(icu::Collator* collator) : collator_(collator) {}

  // Returns true if |n1| preceeds |n2|.
  bool operator()(const BookmarkNode* n1, const BookmarkNode* n2) {
    if (n1->type() == n2->type()) {
      // Types are the same, compare the names.
      if (!collator_)
        return n1->GetTitle() < n2->GetTitle();
      return base::i18n::CompareString16WithCollator(
          collator_, n1->GetTitle(), n2->GetTitle()) == UCOL_LESS;
    }
    // Types differ, sort such that folders come first.
    return n1->is_folder();
  }

 private:
  icu::Collator* collator_;
};

}  // namespace

// BookmarkModel --------------------------------------------------------------

BookmarkModel::BookmarkModel(BookmarkClient* client, bool index_urls)
    : client_(client),
      loaded_(false),
      root_(GURL()),
      bookmark_bar_node_(NULL),
      other_node_(NULL),
      mobile_node_(NULL),
      next_node_id_(1),
      observers_(ObserverList<BookmarkModelObserver>::NOTIFY_EXISTING_ONLY),
      index_urls_(index_urls),
      loaded_signal_(true, false),
      extensive_changes_(0) {
  DCHECK(client_);
}

BookmarkModel::~BookmarkModel() {
  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkModelBeingDeleted(this));

  if (store_.get()) {
    // The store maintains a reference back to us. We need to tell it we're gone
    // so that it doesn't try and invoke a method back on us again.
    store_->BookmarkModelDeleted();
  }
}

void BookmarkModel::Shutdown() {
  if (loaded_)
    return;

  // See comment in HistoryService::ShutdownOnUIThread where this is invoked for
  // details. It is also called when the BookmarkModel is deleted.
  loaded_signal_.Signal();
}

void BookmarkModel::Load(
    PrefService* pref_service,
    const std::string& accept_languages,
    const base::FilePath& profile_path,
    const scoped_refptr<base::SequencedTaskRunner>& io_task_runner,
    const scoped_refptr<base::SequencedTaskRunner>& ui_task_runner) {
  if (store_.get()) {
    // If the store is non-null, it means Load was already invoked. Load should
    // only be invoked once.
    NOTREACHED();
    return;
  }

  expanded_state_tracker_.reset(
      new BookmarkExpandedStateTracker(this, pref_service));

  // Load the bookmarks. BookmarkStorage notifies us when done.
  store_ = new BookmarkStorage(this, profile_path, io_task_runner.get());
  store_->LoadBookmarks(CreateLoadDetails(accept_languages), ui_task_runner);
}

const BookmarkNode* BookmarkModel::GetParentForNewNodes() {
  std::vector<const BookmarkNode*> nodes =
      bookmark_utils::GetMostRecentlyModifiedFolders(this, 1);
  DCHECK(!nodes.empty());  // This list is always padded with default folders.
  return nodes[0];
}

void BookmarkModel::AddObserver(BookmarkModelObserver* observer) {
  observers_.AddObserver(observer);
}

void BookmarkModel::RemoveObserver(BookmarkModelObserver* observer) {
  observers_.RemoveObserver(observer);
}

void BookmarkModel::BeginExtensiveChanges() {
  if (++extensive_changes_ == 1) {
    FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                      ExtensiveBookmarkChangesBeginning(this));
  }
}

void BookmarkModel::EndExtensiveChanges() {
  --extensive_changes_;
  DCHECK_GE(extensive_changes_, 0);
  if (extensive_changes_ == 0) {
    FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                      ExtensiveBookmarkChangesEnded(this));
  }
}

void BookmarkModel::BeginGroupedChanges() {
  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    GroupedBookmarkChangesBeginning(this));
}

void BookmarkModel::EndGroupedChanges() {
  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    GroupedBookmarkChangesEnded(this));
}

void BookmarkModel::Remove(const BookmarkNode* parent, int index) {
  if (!loaded_ || !IsValidIndex(parent, index, false) || is_root_node(parent)) {
    NOTREACHED();
    return;
  }
  RemoveAndDeleteNode(AsMutable(parent->GetChild(index)));
}

void BookmarkModel::RemoveAll() {
  std::set<GURL> removed_urls;
  ScopedVector<BookmarkNode> removed_nodes;

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    OnWillRemoveAllBookmarks(this));

  BeginExtensiveChanges();
  // Skip deleting permanent nodes. Permanent bookmark nodes are the root and
  // its immediate children. For removing all non permanent nodes just remove
  // all children of non-root permanent nodes.
  {
    base::AutoLock url_lock(url_lock_);
    for (int i = 0; i < root_.child_count(); ++i) {
      BookmarkNode* permanent_node = root_.GetChild(i);
      for (int j = permanent_node->child_count() - 1; j >= 0; --j) {
        BookmarkNode* child_node = permanent_node->GetChild(j);
        removed_nodes.push_back(child_node);
        RemoveNodeAndGetRemovedUrls(child_node, &removed_urls);
      }
    }
  }
  EndExtensiveChanges();
  if (store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkAllNodesRemoved(this, removed_urls));
}

void BookmarkModel::Move(const BookmarkNode* node,
                         const BookmarkNode* new_parent,
                         int index) {
  if (!loaded_ || !node || !IsValidIndex(new_parent, index, true) ||
      is_root_node(new_parent) || is_permanent_node(node)) {
    NOTREACHED();
    return;
  }

  if (new_parent->HasAncestor(node)) {
    // Can't make an ancestor of the node be a child of the node.
    NOTREACHED();
    return;
  }

  const BookmarkNode* old_parent = node->parent();
  int old_index = old_parent->GetIndexOf(node);

  if (old_parent == new_parent &&
      (index == old_index || index == old_index + 1)) {
    // Node is already in this position, nothing to do.
    return;
  }

  SetDateFolderModified(new_parent, Time::Now());

  if (old_parent == new_parent && index > old_index)
    index--;
  BookmarkNode* mutable_new_parent = AsMutable(new_parent);
  mutable_new_parent->Add(AsMutable(node), index);

  if (store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkNodeMoved(this, old_parent, old_index,
                                      new_parent, index));
}

void BookmarkModel::Copy(const BookmarkNode* node,
                         const BookmarkNode* new_parent,
                         int index) {
  if (!loaded_ || !node || !IsValidIndex(new_parent, index, true) ||
      is_root_node(new_parent) || is_permanent_node(node)) {
    NOTREACHED();
    return;
  }

  if (new_parent->HasAncestor(node)) {
    // Can't make an ancestor of the node be a child of the node.
    NOTREACHED();
    return;
  }

  SetDateFolderModified(new_parent, Time::Now());
  BookmarkNodeData drag_data(node);
  std::vector<BookmarkNodeData::Element> elements(drag_data.elements);
  // CloneBookmarkNode will use BookmarkModel methods to do the job, so we
  // don't need to send notifications here.
  bookmark_utils::CloneBookmarkNode(this, elements, new_parent, index, true);

  if (store_.get())
    store_->ScheduleSave();
}

const gfx::Image& BookmarkModel::GetFavicon(const BookmarkNode* node) {
  DCHECK(node);
  if (node->favicon_state() == BookmarkNode::INVALID_FAVICON) {
    BookmarkNode* mutable_node = AsMutable(node);
    LoadFavicon(
        mutable_node,
        client_->PreferTouchIcon() ?
            favicon_base::TOUCH_ICON :
            favicon_base::FAVICON);
  }
  return node->favicon();
}

favicon_base::IconType BookmarkModel::GetFaviconType(const BookmarkNode* node) {
  DCHECK(node);
  return node->favicon_type();
}

void BookmarkModel::SetTitle(const BookmarkNode* node,
                             const base::string16& title) {
  if (!node) {
    NOTREACHED();
    return;
  }
  if (node->GetTitle() == title)
    return;

  if (is_permanent_node(node)) {
    NOTREACHED();
    return;
  }

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    OnWillChangeBookmarkNode(this, node));

  // The title index doesn't support changing the title, instead we remove then
  // add it back.
  index_->Remove(node);
  AsMutable(node)->SetTitle(title);
  index_->Add(node);

  if (store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkNodeChanged(this, node));
}

void BookmarkModel::SetURL(const BookmarkNode* node, const GURL& url) {
  if (!node) {
    NOTREACHED();
    return;
  }

  // We cannot change the URL of a folder.
  if (node->is_folder()) {
    NOTREACHED();
    return;
  }

  if (node->url() == url)
    return;

  BookmarkNode* mutable_node = AsMutable(node);
  mutable_node->InvalidateFavicon();
  CancelPendingFaviconLoadRequests(mutable_node);

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    OnWillChangeBookmarkNode(this, node));

  {
    base::AutoLock url_lock(url_lock_);
    RemoveNodeFromURLSet(mutable_node);
    mutable_node->set_url(url);
    nodes_ordered_by_url_set_.insert(mutable_node);
  }

  if (store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkNodeChanged(this, node));
}

void BookmarkModel::SetNodeMetaInfo(const BookmarkNode* node,
                                    const std::string& key,
                                    const std::string& value) {
  std::string old_value;
  if (node->GetMetaInfo(key, &old_value) && old_value == value)
    return;

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    OnWillChangeBookmarkMetaInfo(this, node));

  if (AsMutable(node)->SetMetaInfo(key, value) && store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkMetaInfoChanged(this, node));
}

void BookmarkModel::SetNodeMetaInfoMap(
    const BookmarkNode* node,
    const BookmarkNode::MetaInfoMap& meta_info_map) {
  const BookmarkNode::MetaInfoMap* old_meta_info_map = node->GetMetaInfoMap();
  if ((!old_meta_info_map && meta_info_map.empty()) ||
      (old_meta_info_map && meta_info_map == *old_meta_info_map))
    return;

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    OnWillChangeBookmarkMetaInfo(this, node));

  AsMutable(node)->SetMetaInfoMap(meta_info_map);
  if (store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkMetaInfoChanged(this, node));
}

void BookmarkModel::DeleteNodeMetaInfo(const BookmarkNode* node,
                                       const std::string& key) {
  const BookmarkNode::MetaInfoMap* meta_info_map = node->GetMetaInfoMap();
  if (!meta_info_map || meta_info_map->find(key) == meta_info_map->end())
    return;

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    OnWillChangeBookmarkMetaInfo(this, node));

  if (AsMutable(node)->DeleteMetaInfo(key) && store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkMetaInfoChanged(this, node));
}

void BookmarkModel::SetNodeSyncTransactionVersion(
    const BookmarkNode* node,
    int64 sync_transaction_version) {
  if (sync_transaction_version == node->sync_transaction_version())
    return;

  AsMutable(node)->set_sync_transaction_version(sync_transaction_version);
  if (store_.get())
    store_->ScheduleSave();
}

void BookmarkModel::OnFaviconChanged(const std::set<GURL>& urls) {
  // Ignore events if |Load| has not been called yet.
  if (!store_)
    return;

  // Prevent the observers from getting confused for multiple favicon loads.
  for (std::set<GURL>::const_iterator i = urls.begin(); i != urls.end(); ++i) {
    std::vector<const BookmarkNode*> nodes;
    GetNodesByURL(*i, &nodes);
    for (size_t i = 0; i < nodes.size(); ++i) {
      // Got an updated favicon, for a URL, do a new request.
      BookmarkNode* node = AsMutable(nodes[i]);
      node->InvalidateFavicon();
      CancelPendingFaviconLoadRequests(node);
      FOR_EACH_OBSERVER(BookmarkModelObserver,
                        observers_,
                        BookmarkNodeFaviconChanged(this, node));
    }
  }
}

void BookmarkModel::SetDateAdded(const BookmarkNode* node,
                                 Time date_added) {
  if (!node) {
    NOTREACHED();
    return;
  }

  if (node->date_added() == date_added)
    return;

  if (is_permanent_node(node)) {
    NOTREACHED();
    return;
  }

  AsMutable(node)->set_date_added(date_added);

  // Syncing might result in dates newer than the folder's last modified date.
  if (date_added > node->parent()->date_folder_modified()) {
    // Will trigger store_->ScheduleSave().
    SetDateFolderModified(node->parent(), date_added);
  } else if (store_.get()) {
    store_->ScheduleSave();
  }
}

void BookmarkModel::GetNodesByURL(const GURL& url,
                                  std::vector<const BookmarkNode*>* nodes) {
  base::AutoLock url_lock(url_lock_);
  BookmarkNode tmp_node(url);
  NodesOrderedByURLSet::iterator i = nodes_ordered_by_url_set_.find(&tmp_node);
  while (i != nodes_ordered_by_url_set_.end() && (*i)->url() == url) {
    nodes->push_back(*i);
    ++i;
  }
}

const BookmarkNode* BookmarkModel::GetMostRecentlyAddedNodeForURL(
    const GURL& url) {
  std::vector<const BookmarkNode*> nodes;
  GetNodesByURL(url, &nodes);
  if (nodes.empty())
    return NULL;

  std::sort(nodes.begin(), nodes.end(), &bookmark_utils::MoreRecentlyAdded);
  return nodes.front();
}

bool BookmarkModel::HasBookmarks() {
  base::AutoLock url_lock(url_lock_);
  return !nodes_ordered_by_url_set_.empty();
}

bool BookmarkModel::IsBookmarked(const GURL& url) {
  base::AutoLock url_lock(url_lock_);
  return IsBookmarkedNoLock(url);
}

void BookmarkModel::GetBookmarks(
    std::vector<BookmarkService::URLAndTitle>* bookmarks) {
  base::AutoLock url_lock(url_lock_);
  const GURL* last_url = NULL;
  for (NodesOrderedByURLSet::iterator i = nodes_ordered_by_url_set_.begin();
       i != nodes_ordered_by_url_set_.end(); ++i) {
    const GURL* url = &((*i)->url());
    // Only add unique URLs.
    if (!last_url || *url != *last_url) {
      BookmarkService::URLAndTitle bookmark;
      bookmark.url = *url;
      bookmark.title = (*i)->GetTitle();
      bookmarks->push_back(bookmark);
    }
    last_url = url;
  }
}

void BookmarkModel::BlockTillLoaded() {
  loaded_signal_.Wait();
}

const BookmarkNode* BookmarkModel::AddFolder(const BookmarkNode* parent,
                                             int index,
                                             const base::string16& title) {
  return AddFolderWithMetaInfo(parent, index, title, NULL);
}
const BookmarkNode* BookmarkModel::AddFolderWithMetaInfo(
    const BookmarkNode* parent,
    int index,
    const base::string16& title,
    const BookmarkNode::MetaInfoMap* meta_info) {
  if (!loaded_ || is_root_node(parent) || !IsValidIndex(parent, index, true)) {
    // Can't add to the root.
    NOTREACHED();
    return NULL;
  }

  BookmarkNode* new_node = new BookmarkNode(generate_next_node_id(), GURL());
  new_node->set_date_folder_modified(Time::Now());
  // Folders shouldn't have line breaks in their titles.
  new_node->SetTitle(title);
  new_node->set_type(BookmarkNode::FOLDER);
  if (meta_info)
    new_node->SetMetaInfoMap(*meta_info);

  return AddNode(AsMutable(parent), index, new_node);
}

const BookmarkNode* BookmarkModel::AddURL(const BookmarkNode* parent,
                                          int index,
                                          const base::string16& title,
                                          const GURL& url) {
  return AddURLWithCreationTimeAndMetaInfo(
      parent,
      index,
      base::CollapseWhitespace(title, false),
      url,
      Time::Now(),
      NULL);
}

const BookmarkNode* BookmarkModel::AddURLWithCreationTimeAndMetaInfo(
    const BookmarkNode* parent,
    int index,
    const base::string16& title,
    const GURL& url,
    const Time& creation_time,
    const BookmarkNode::MetaInfoMap* meta_info) {
  if (!loaded_ || !url.is_valid() || is_root_node(parent) ||
      !IsValidIndex(parent, index, true)) {
    NOTREACHED();
    return NULL;
  }

  // Syncing may result in dates newer than the last modified date.
  if (creation_time > parent->date_folder_modified())
    SetDateFolderModified(parent, creation_time);

  BookmarkNode* new_node = new BookmarkNode(generate_next_node_id(), url);
  new_node->SetTitle(title);
  new_node->set_date_added(creation_time);
  new_node->set_type(BookmarkNode::URL);
  if (meta_info)
    new_node->SetMetaInfoMap(*meta_info);

  {
    // Only hold the lock for the duration of the insert.
    base::AutoLock url_lock(url_lock_);
    nodes_ordered_by_url_set_.insert(new_node);
  }

  return AddNode(AsMutable(parent), index, new_node);
}

void BookmarkModel::SortChildren(const BookmarkNode* parent) {
  if (!parent || !parent->is_folder() || is_root_node(parent) ||
      parent->child_count() <= 1) {
    return;
  }

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    OnWillReorderBookmarkNode(this, parent));

  UErrorCode error = U_ZERO_ERROR;
  scoped_ptr<icu::Collator> collator(icu::Collator::createInstance(error));
  if (U_FAILURE(error))
    collator.reset(NULL);
  BookmarkNode* mutable_parent = AsMutable(parent);
  std::sort(mutable_parent->children().begin(),
            mutable_parent->children().end(),
            SortComparator(collator.get()));

  if (store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkNodeChildrenReordered(this, parent));
}

void BookmarkModel::ReorderChildren(
    const BookmarkNode* parent,
    const std::vector<const BookmarkNode*>& ordered_nodes) {
  // Ensure that all children in |parent| are in |ordered_nodes|.
  DCHECK_EQ(static_cast<size_t>(parent->child_count()), ordered_nodes.size());
  for (size_t i = 0; i < ordered_nodes.size(); ++i)
    DCHECK_EQ(parent, ordered_nodes[i]->parent());

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    OnWillReorderBookmarkNode(this, parent));

  AsMutable(parent)->SetChildren(
      *(reinterpret_cast<const std::vector<BookmarkNode*>*>(&ordered_nodes)));

  if (store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkNodeChildrenReordered(this, parent));
}

void BookmarkModel::SetDateFolderModified(const BookmarkNode* parent,
                                          const Time time) {
  DCHECK(parent);
  AsMutable(parent)->set_date_folder_modified(time);

  if (store_.get())
    store_->ScheduleSave();
}

void BookmarkModel::ResetDateFolderModified(const BookmarkNode* node) {
  SetDateFolderModified(node, Time());
}

void BookmarkModel::GetBookmarksMatching(
    const base::string16& text,
    size_t max_count,
    std::vector<BookmarkMatch>* matches) {
  if (!loaded_)
    return;

  index_->GetBookmarksMatching(text, max_count, matches);
}

void BookmarkModel::ClearStore() {
  store_ = NULL;
}

void BookmarkModel::SetPermanentNodeVisible(BookmarkNode::Type type,
                                            bool value) {
  AsMutable(PermanentNode(type))->set_visible(
      value || client_->IsPermanentNodeVisible(type));
}

const BookmarkPermanentNode* BookmarkModel::PermanentNode(
    BookmarkNode::Type type) {
  DCHECK(loaded_);
  switch (type) {
    case BookmarkNode::BOOKMARK_BAR:
      return bookmark_bar_node_;
    case BookmarkNode::OTHER_NODE:
      return other_node_;
    case BookmarkNode::MOBILE:
      return mobile_node_;
    default:
      NOTREACHED();
      return NULL;
  }
}

bool BookmarkModel::IsBookmarkedNoLock(const GURL& url) {
  BookmarkNode tmp_node(url);
  return (nodes_ordered_by_url_set_.find(&tmp_node) !=
          nodes_ordered_by_url_set_.end());
}

void BookmarkModel::RemoveNode(BookmarkNode* node,
                               std::set<GURL>* removed_urls) {
  if (!loaded_ || !node || is_permanent_node(node)) {
    NOTREACHED();
    return;
  }

  url_lock_.AssertAcquired();
  if (node->is_url()) {
    RemoveNodeFromURLSet(node);
    removed_urls->insert(node->url());
    index_->Remove(node);
  }

  CancelPendingFaviconLoadRequests(node);

  // Recurse through children.
  for (int i = node->child_count() - 1; i >= 0; --i)
    RemoveNode(node->GetChild(i), removed_urls);
}

void BookmarkModel::DoneLoading(scoped_ptr<BookmarkLoadDetails> details) {
  DCHECK(details);
  if (loaded_) {
    // We should only ever be loaded once.
    NOTREACHED();
    return;
  }

  next_node_id_ = details->max_id();
  if (details->computed_checksum() != details->stored_checksum() ||
      details->ids_reassigned()) {
    // If bookmarks file changed externally, the IDs may have changed
    // externally. In that case, the decoder may have reassigned IDs to make
    // them unique. So when the file has changed externally, we should save the
    // bookmarks file to persist new IDs.
    if (store_.get())
      store_->ScheduleSave();
  }
  bookmark_bar_node_ = details->release_bb_node();
  other_node_ = details->release_other_folder_node();
  mobile_node_ = details->release_mobile_folder_node();
  index_.reset(details->release_index());

  // WARNING: order is important here, various places assume the order is
  // constant (but can vary between embedders with the initial visibility
  // of permanent nodes).
  BookmarkPermanentNode* root_children[] = {
      bookmark_bar_node_, other_node_, mobile_node_,
  };
  std::stable_sort(root_children,
                   root_children + arraysize(root_children),
                   VisibilityComparator(client_));
  for (size_t i = 0; i < arraysize(root_children); ++i) {
    root_.Add(root_children[i], static_cast<int>(i));
  }

  root_.SetMetaInfoMap(details->model_meta_info_map());
  root_.set_sync_transaction_version(details->model_sync_transaction_version());

  {
    base::AutoLock url_lock(url_lock_);
    // Update nodes_ordered_by_url_set_ from the nodes.
    PopulateNodesByURL(&root_);
  }

  loaded_ = true;

  loaded_signal_.Signal();

  // Notify our direct observers.
  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkModelLoaded(this, details->ids_reassigned()));
}

void BookmarkModel::RemoveAndDeleteNode(BookmarkNode* delete_me) {
  scoped_ptr<BookmarkNode> node(delete_me);

  const BookmarkNode* parent = node->parent();
  DCHECK(parent);
  int index = parent->GetIndexOf(node.get());

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    OnWillRemoveBookmarks(this, parent, index, node.get()));

  std::set<GURL> removed_urls;
  {
    base::AutoLock url_lock(url_lock_);
    RemoveNodeAndGetRemovedUrls(node.get(), &removed_urls);
  }

  if (store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(
      BookmarkModelObserver,
      observers_,
      BookmarkNodeRemoved(this, parent, index, node.get(), removed_urls));
}

void BookmarkModel::RemoveNodeFromURLSet(BookmarkNode* node) {
  // NOTE: this is called in such a way that url_lock_ is already held. As
  // such, this doesn't explicitly grab the lock.
  NodesOrderedByURLSet::iterator i = nodes_ordered_by_url_set_.find(node);
  DCHECK(i != nodes_ordered_by_url_set_.end());
  // i points to the first node with the URL, advance until we find the
  // node we're removing.
  while (*i != node)
    ++i;
  nodes_ordered_by_url_set_.erase(i);
}

void BookmarkModel::RemoveNodeAndGetRemovedUrls(BookmarkNode* node,
                                                std::set<GURL>* removed_urls) {
  // NOTE: this method should be always called with |url_lock_| held.
  // This method does not explicitly acquires a lock.
  url_lock_.AssertAcquired();
  DCHECK(removed_urls);
  BookmarkNode* parent = AsMutable(node->parent());
  DCHECK(parent);
  parent->Remove(node);
  RemoveNode(node, removed_urls);
  // RemoveNode adds an entry to removed_urls for each node of type URL. As we
  // allow duplicates we need to remove any entries that are still bookmarked.
  for (std::set<GURL>::iterator i = removed_urls->begin();
       i != removed_urls->end();) {
    if (IsBookmarkedNoLock(*i)) {
      // When we erase the iterator pointing at the erasee is
      // invalidated, so using i++ here within the "erase" call is
      // important as it advances the iterator before passing the
      // old value through to erase.
      removed_urls->erase(i++);
    } else {
      ++i;
    }
  }
}

BookmarkNode* BookmarkModel::AddNode(BookmarkNode* parent,
                                     int index,
                                     BookmarkNode* node) {
  parent->Add(node, index);

  if (store_.get())
    store_->ScheduleSave();

  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkNodeAdded(this, parent, index));

  index_->Add(node);

  return node;
}

bool BookmarkModel::IsValidIndex(const BookmarkNode* parent,
                                 int index,
                                 bool allow_end) {
  return (parent && parent->is_folder() &&
          (index >= 0 && (index < parent->child_count() ||
                          (allow_end && index == parent->child_count()))));
}

BookmarkPermanentNode* BookmarkModel::CreatePermanentNode(
    BookmarkNode::Type type) {
  DCHECK(type == BookmarkNode::BOOKMARK_BAR ||
         type == BookmarkNode::OTHER_NODE ||
         type == BookmarkNode::MOBILE);
  BookmarkPermanentNode* node =
      new BookmarkPermanentNode(generate_next_node_id());
  node->set_visible(client_->IsPermanentNodeVisible(type));

  int title_id;
  switch (type) {
    case BookmarkNode::BOOKMARK_BAR:
      title_id = IDS_BOOKMARK_BAR_FOLDER_NAME;
      break;
    case BookmarkNode::OTHER_NODE:
      title_id = IDS_BOOKMARK_BAR_OTHER_FOLDER_NAME;
      break;
    case BookmarkNode::MOBILE:
      title_id = IDS_BOOKMARK_BAR_MOBILE_FOLDER_NAME;
      break;
    default:
      NOTREACHED();
      title_id = IDS_BOOKMARK_BAR_FOLDER_NAME;
      break;
  }
  node->SetTitle(l10n_util::GetStringUTF16(title_id));
  node->set_type(type);
  return node;
}

void BookmarkModel::OnFaviconDataAvailable(
    BookmarkNode* node,
    favicon_base::IconType icon_type,
    const favicon_base::FaviconImageResult& image_result) {
  DCHECK(node);
  node->set_favicon_load_task_id(base::CancelableTaskTracker::kBadTaskId);
  node->set_favicon_state(BookmarkNode::LOADED_FAVICON);
  if (!image_result.image.IsEmpty()) {
    node->set_favicon_type(icon_type);
    node->set_favicon(image_result.image);
    node->set_icon_url(image_result.icon_url);
    FaviconLoaded(node);
  } else if (icon_type == favicon_base::TOUCH_ICON) {
    // Couldn't load the touch icon, fallback to the regular favicon.
    DCHECK(client_->PreferTouchIcon());
    LoadFavicon(node, favicon_base::FAVICON);
  }
}

void BookmarkModel::LoadFavicon(
    BookmarkNode* node,
    favicon_base::IconType icon_type) {
  if (node->is_folder())
    return;

  DCHECK(node->url().is_valid());
  node->set_favicon_state(BookmarkNode::LOADING_FAVICON);
  base::CancelableTaskTracker::TaskId taskId = client_->GetFaviconImageForURL(
      node->url(),
      icon_type,
      icon_type == favicon_base::FAVICON ? gfx::kFaviconSize : 0,
      base::Bind(
          &BookmarkModel::OnFaviconDataAvailable,
          base::Unretained(this),
          node,
          icon_type),
      &cancelable_task_tracker_);
  if (taskId != base::CancelableTaskTracker::kBadTaskId)
    node->set_favicon_load_task_id(taskId);
}

void BookmarkModel::FaviconLoaded(const BookmarkNode* node) {
  FOR_EACH_OBSERVER(BookmarkModelObserver, observers_,
                    BookmarkNodeFaviconChanged(this, node));
}

void BookmarkModel::CancelPendingFaviconLoadRequests(BookmarkNode* node) {
  if (node->favicon_load_task_id() != base::CancelableTaskTracker::kBadTaskId) {
    cancelable_task_tracker_.TryCancel(node->favicon_load_task_id());
    node->set_favicon_load_task_id(base::CancelableTaskTracker::kBadTaskId);
  }
}

void BookmarkModel::PopulateNodesByURL(BookmarkNode* node) {
  // NOTE: this is called with url_lock_ already held. As such, this doesn't
  // explicitly grab the lock.
  if (node->is_url())
    nodes_ordered_by_url_set_.insert(node);
  for (int i = 0; i < node->child_count(); ++i)
    PopulateNodesByURL(node->GetChild(i));
}

int64 BookmarkModel::generate_next_node_id() {
  return next_node_id_++;
}

scoped_ptr<BookmarkLoadDetails> BookmarkModel::CreateLoadDetails(
    const std::string& accept_languages) {
  BookmarkPermanentNode* bb_node =
      CreatePermanentNode(BookmarkNode::BOOKMARK_BAR);
  BookmarkPermanentNode* other_node =
      CreatePermanentNode(BookmarkNode::OTHER_NODE);
  BookmarkPermanentNode* mobile_node =
      CreatePermanentNode(BookmarkNode::MOBILE);
  return scoped_ptr<BookmarkLoadDetails>(new BookmarkLoadDetails(
      bb_node,
      other_node,
      mobile_node,
      new BookmarkIndex(client_, index_urls_, accept_languages),
      next_node_id_));
}
