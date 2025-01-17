// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_BOOKMARKS_CORE_BROWSER_BOOKMARK_MODEL_H_
#define COMPONENTS_BOOKMARKS_CORE_BROWSER_BOOKMARK_MODEL_H_

#include <map>
#include <set>
#include <vector>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "base/strings/string16.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "components/bookmarks/core/browser/bookmark_client.h"
#include "components/bookmarks/core/browser/bookmark_node.h"
#include "components/bookmarks/core/browser/bookmark_service.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"

class BookmarkExpandedStateTracker;
class BookmarkIndex;
class BookmarkLoadDetails;
class BookmarkModelObserver;
class BookmarkStorage;
struct BookmarkMatch;
class PrefService;
class ScopedGroupBookmarkActions;

namespace base {
class FilePath;
class SequencedTaskRunner;
}

namespace favicon_base {
struct FaviconImageResult;
}

namespace test {
class TestBookmarkClient;
}

// BookmarkModel --------------------------------------------------------------

// BookmarkModel provides a directed acyclic graph of URLs and folders.
// Three graphs are provided for the three entry points: those on the 'bookmarks
// bar', those in the 'other bookmarks' folder and those in the 'mobile' folder.
//
// An observer may be attached to observe relevant events.
//
// You should NOT directly create a BookmarkModel, instead go through the
// BookmarkModelFactory.
class BookmarkModel : public BookmarkService {
 public:
  // |index_urls| says whether URLs should be stored in the BookmarkIndex
  // in addition to bookmark titles.
  BookmarkModel(BookmarkClient* client, bool index_urls);
  virtual ~BookmarkModel();

  // Invoked prior to destruction to release any necessary resources.
  void Shutdown();

  // Loads the bookmarks. This is called upon creation of the
  // BookmarkModel. You need not invoke this directly.
  // All load operations will be executed on |io_task_runner| and the completion
  // callback will be called from |ui_task_runner|.
  void Load(PrefService* pref_service,
            const std::string& accept_languages,
            const base::FilePath& profile_path,
            const scoped_refptr<base::SequencedTaskRunner>& io_task_runner,
            const scoped_refptr<base::SequencedTaskRunner>& ui_task_runner);

  // Returns true if the model finished loading.
  bool loaded() const { return loaded_; }

  // Returns the root node. The 'bookmark bar' node and 'other' node are
  // children of the root node.
  const BookmarkNode* root_node() const { return &root_; }

  // Returns the 'bookmark bar' node. This is NULL until loaded.
  const BookmarkNode* bookmark_bar_node() const { return bookmark_bar_node_; }

  // Returns the 'other' node. This is NULL until loaded.
  const BookmarkNode* other_node() const { return other_node_; }

  // Returns the 'mobile' node. This is NULL until loaded.
  const BookmarkNode* mobile_node() const { return mobile_node_; }

  bool is_root_node(const BookmarkNode* node) const { return node == &root_; }

  // Returns whether the given |node| is one of the permanent nodes - root node,
  // 'bookmark bar' node, 'other' node or 'mobile' node.
  bool is_permanent_node(const BookmarkNode* node) const {
    return node == &root_ ||
           node == bookmark_bar_node_ ||
           node == other_node_ ||
           node == mobile_node_;
  }

  // Returns the parent the last node was added to. This never returns NULL
  // (as long as the model is loaded).
  const BookmarkNode* GetParentForNewNodes();

  void AddObserver(BookmarkModelObserver* observer);
  void RemoveObserver(BookmarkModelObserver* observer);

  // Notifies the observers that an extensive set of changes is about to happen,
  // such as during import or sync, so they can delay any expensive UI updates
  // until it's finished.
  void BeginExtensiveChanges();
  void EndExtensiveChanges();

  // Returns true if this bookmark model is currently in a mode where extensive
  // changes might happen, such as for import and sync. This is helpful for
  // observers that are created after the mode has started, and want to check
  // state during their own initializer, such as the NTP.
  bool IsDoingExtensiveChanges() const { return extensive_changes_ > 0; }

  // Removes the node at the given |index| from |parent|. Removing a folder node
  // recursively removes all nodes. Observers are notified immediately.
  void Remove(const BookmarkNode* parent, int index);

  // Removes all the non-permanent bookmark nodes. Observers are only notified
  // when all nodes have been removed. There is no notification for individual
  // node removals.
  void RemoveAll();

  // Moves |node| to |new_parent| and inserts it at the given |index|.
  void Move(const BookmarkNode* node,
            const BookmarkNode* new_parent,
            int index);

  // Inserts a copy of |node| into |new_parent| at |index|.
  void Copy(const BookmarkNode* node,
            const BookmarkNode* new_parent,
            int index);

  // Returns the favicon for |node|. If the favicon has not yet been
  // loaded it is loaded and the observer of the model notified when done.
  const gfx::Image& GetFavicon(const BookmarkNode* node);

  // Returns the type of the favicon for |node|. If the favicon has not yet
  // been loaded, it returns |favicon_base::INVALID_ICON|.
  favicon_base::IconType GetFaviconType(const BookmarkNode* node);

  // Sets the title of |node|.
  void SetTitle(const BookmarkNode* node, const base::string16& title);

  // Sets the URL of |node|.
  void SetURL(const BookmarkNode* node, const GURL& url);

  // Sets the date added time of |node|.
  void SetDateAdded(const BookmarkNode* node, base::Time date_added);

  // Returns the set of nodes with the |url|.
  void GetNodesByURL(const GURL& url, std::vector<const BookmarkNode*>* nodes);

  // Returns the most recently added node for the |url|. Returns NULL if |url|
  // is not bookmarked.
  const BookmarkNode* GetMostRecentlyAddedNodeForURL(const GURL& url);

  // Returns true if there are bookmarks, otherwise returns false.
  // This method is thread safe.
  bool HasBookmarks();

  // Returns true if there is a bookmark with the |url|.
  // This method is thread safe.
  // See BookmarkService for more details on this.
  virtual bool IsBookmarked(const GURL& url) OVERRIDE;

  // Returns all the bookmarked urls and their titles.
  // This method is thread safe.
  // See BookmarkService for more details on this.
  virtual void GetBookmarks(
      std::vector<BookmarkService::URLAndTitle>* urls) OVERRIDE;

  // Blocks until loaded; this is NOT invoked on the main thread.
  // See BookmarkService for more details on this.
  virtual void BlockTillLoaded() OVERRIDE;

  // Adds a new folder node at the specified position.
  const BookmarkNode* AddFolder(const BookmarkNode* parent,
                                int index,
                                const base::string16& title);

  // Adds a new folder with meta info.
  const BookmarkNode* AddFolderWithMetaInfo(
      const BookmarkNode* parent,
      int index,
      const base::string16& title,
      const BookmarkNode::MetaInfoMap* meta_info);

  // Adds a url at the specified position.
  const BookmarkNode* AddURL(const BookmarkNode* parent,
                             int index,
                             const base::string16& title,
                             const GURL& url);

  // Adds a url with a specific creation date and meta info.
  const BookmarkNode* AddURLWithCreationTimeAndMetaInfo(
      const BookmarkNode* parent,
      int index,
      const base::string16& title,
      const GURL& url,
      const base::Time& creation_time,
      const BookmarkNode::MetaInfoMap* meta_info);

  // Sorts the children of |parent|, notifying observers by way of the
  // BookmarkNodeChildrenReordered method.
  void SortChildren(const BookmarkNode* parent);

  // Order the children of |parent| as specified in |ordered_nodes|.  This
  // function should only be used to reorder the child nodes of |parent| and
  // is not meant to move nodes between different parent. Notifies observers
  // using the BookmarkNodeChildrenReordered method.
  void ReorderChildren(const BookmarkNode* parent,
                       const std::vector<const BookmarkNode*>& ordered_nodes);

  // Sets the date when the folder was modified.
  void SetDateFolderModified(const BookmarkNode* node, const base::Time time);

  // Resets the 'date modified' time of the node to 0. This is used during
  // importing to exclude the newly created folders from showing up in the
  // combobox of most recently modified folders.
  void ResetDateFolderModified(const BookmarkNode* node);

  // Returns up to |max_count| of bookmarks containing each term from |text|
  // in either the title or the URL.
  void GetBookmarksMatching(
      const base::string16& text,
      size_t max_count,
      std::vector<BookmarkMatch>* matches);

  // Sets the store to NULL, making it so the BookmarkModel does not persist
  // any changes to disk. This is only useful during testing to speed up
  // testing.
  void ClearStore();

  // Returns the next node ID.
  int64 next_node_id() const { return next_node_id_; }

  // Returns the object responsible for tracking the set of expanded nodes in
  // the bookmark editor.
  BookmarkExpandedStateTracker* expanded_state_tracker() {
    return expanded_state_tracker_.get();
  }

  // Sets the visibility of one of the permanent nodes (unless the node must
  // always be visible, see |BookmarkClient::IsPermanentNodeVisible| for more
  // details). This is set by sync.
  void SetPermanentNodeVisible(BookmarkNode::Type type, bool value);

  // Returns the permanent node of type |type|.
  const BookmarkPermanentNode* PermanentNode(BookmarkNode::Type type);

  // Sets/deletes meta info of |node|.
  void SetNodeMetaInfo(const BookmarkNode* node,
                       const std::string& key,
                       const std::string& value);
  void SetNodeMetaInfoMap(const BookmarkNode* node,
                          const BookmarkNode::MetaInfoMap& meta_info_map);
  void DeleteNodeMetaInfo(const BookmarkNode* node,
                          const std::string& key);

  // Sets the sync transaction version of |node|.
  void SetNodeSyncTransactionVersion(const BookmarkNode* node,
                                     int64 sync_transaction_version);

  // Notify BookmarkModel that the favicons for |urls| have changed and have to
  // be refetched. This notification is sent by BookmarkClient.
  void OnFaviconChanged(const std::set<GURL>& urls);

  // Returns the client used by this BookmarkModel.
  BookmarkClient* client() const { return client_; }

 private:
  friend class BookmarkCodecTest;
  friend class BookmarkModelTest;
  friend class BookmarkStorage;
  friend class ScopedGroupBookmarkActions;
  friend class test::TestBookmarkClient;

  // Used to order BookmarkNodes by URL.
  class NodeURLComparator {
   public:
    bool operator()(const BookmarkNode* n1, const BookmarkNode* n2) const {
      return n1->url() < n2->url();
    }
  };

  // Implementation of IsBookmarked. Before calling this the caller must obtain
  // a lock on |url_lock_|.
  bool IsBookmarkedNoLock(const GURL& url);

  // Removes the node from internal maps and recurses through all children. If
  // the node is a url, its url is added to removed_urls.
  //
  // This does NOT delete the node.
  void RemoveNode(BookmarkNode* node, std::set<GURL>* removed_urls);

  // Invoked when loading is finished. Sets |loaded_| and notifies observers.
  // BookmarkModel takes ownership of |details|.
  void DoneLoading(scoped_ptr<BookmarkLoadDetails> details);

  // Populates |nodes_ordered_by_url_set_| from root.
  void PopulateNodesByURL(BookmarkNode* node);

  // Removes the node from its parent, but does not delete it. No notifications
  // are sent. |removed_urls| is populated with the urls which no longer have
  // any bookmarks associated with them.
  // This method should be called after acquiring |url_lock_|.
  void RemoveNodeAndGetRemovedUrls(BookmarkNode* node,
                                   std::set<GURL>* removed_urls);

  // Removes the node from its parent, sends notification, and deletes it.
  // type specifies how the node should be removed.
  void RemoveAndDeleteNode(BookmarkNode* delete_me);

  // Remove |node| from |nodes_ordered_by_url_set_|.
  void RemoveNodeFromURLSet(BookmarkNode* node);

  // Adds the |node| at |parent| in the specified |index| and notifies its
  // observers.
  BookmarkNode* AddNode(BookmarkNode* parent,
                        int index,
                        BookmarkNode* node);

  // Returns true if the parent and index are valid.
  bool IsValidIndex(const BookmarkNode* parent, int index, bool allow_end);

  // Creates one of the possible permanent nodes (bookmark bar node, other node
  // and mobile node) from |type|.
  BookmarkPermanentNode* CreatePermanentNode(BookmarkNode::Type type);

  // Notification that a favicon has finished loading. If we can decode the
  // favicon, FaviconLoaded is invoked.
  void OnFaviconDataAvailable(
      BookmarkNode* node,
      favicon_base::IconType icon_type,
      const favicon_base::FaviconImageResult& image_result);

  // Invoked from the node to load the favicon. Requests the favicon from the
  // favicon service.
  void LoadFavicon(BookmarkNode* node, favicon_base::IconType icon_type);

  // Called to notify the observers that the favicon has been loaded.
  void FaviconLoaded(const BookmarkNode* node);

  // If we're waiting on a favicon for node, the load request is canceled.
  void CancelPendingFaviconLoadRequests(BookmarkNode* node);

  // Notifies the observers that a set of changes initiated by a single user
  // action is about to happen and has completed.
  void BeginGroupedChanges();
  void EndGroupedChanges();

  // Generates and returns the next node ID.
  int64 generate_next_node_id();

  // Sets the maximum node ID to the given value.
  // This is used by BookmarkCodec to report the maximum ID after it's done
  // decoding since during decoding codec assigns node IDs.
  void set_next_node_id(int64 id) { next_node_id_ = id; }

  // Creates and returns a new BookmarkLoadDetails. It's up to the caller to
  // delete the returned object.
  scoped_ptr<BookmarkLoadDetails> CreateLoadDetails(
      const std::string& accept_languages);

  BookmarkClient* const client_;

  // Whether the initial set of data has been loaded.
  bool loaded_;

  // The root node. This contains the bookmark bar node, the 'other' node and
  // the mobile node as children.
  BookmarkNode root_;

  BookmarkPermanentNode* bookmark_bar_node_;
  BookmarkPermanentNode* other_node_;
  BookmarkPermanentNode* mobile_node_;

  // The maximum ID assigned to the bookmark nodes in the model.
  int64 next_node_id_;

  // The observers.
  ObserverList<BookmarkModelObserver> observers_;

  // Set of nodes ordered by URL. This is not a map to avoid copying the
  // urls.
  // WARNING: |nodes_ordered_by_url_set_| is accessed on multiple threads. As
  // such, be sure and wrap all usage of it around |url_lock_|.
  typedef std::multiset<BookmarkNode*, NodeURLComparator> NodesOrderedByURLSet;
  NodesOrderedByURLSet nodes_ordered_by_url_set_;
  base::Lock url_lock_;

  // Used for loading favicons.
  base::CancelableTaskTracker cancelable_task_tracker_;

  // Reads/writes bookmarks to disk.
  scoped_refptr<BookmarkStorage> store_;

  scoped_ptr<BookmarkIndex> index_;

  // True if URLs are stored in the BookmarkIndex in addition to bookmark
  // titles.
  const bool index_urls_;

  base::WaitableEvent loaded_signal_;

  // See description of IsDoingExtensiveChanges above.
  int extensive_changes_;

  scoped_ptr<BookmarkExpandedStateTracker> expanded_state_tracker_;

  DISALLOW_COPY_AND_ASSIGN(BookmarkModel);
};

#endif  // COMPONENTS_BOOKMARKS_CORE_BROWSER_BOOKMARK_MODEL_H_
