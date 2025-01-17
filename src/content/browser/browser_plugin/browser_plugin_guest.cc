// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/browser_plugin/browser_plugin_guest.h"

#include <algorithm>

#include "base/message_loop/message_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "content/browser/browser_plugin/browser_plugin_embedder.h"
#include "content/browser/browser_plugin/browser_plugin_host_factory.h"
#include "content/browser/browser_thread_impl.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/frame_host/render_frame_host_impl.h"
#include "content/browser/frame_host/render_widget_host_view_guest.h"
#include "content/browser/loader/resource_dispatcher_host_impl.h"
#include "content/browser/renderer_host/render_view_host_delegate_view.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/browser/web_contents/web_contents_view_guest.h"
#include "content/common/browser_plugin/browser_plugin_constants.h"
#include "content/common/browser_plugin/browser_plugin_messages.h"
#include "content/common/content_constants_internal.h"
#include "content/common/drag_messages.h"
#include "content/common/input_messages.h"
#include "content/common/view_messages.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_plugin_guest_manager.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/context_menu_params.h"
#include "content/public/common/drop_data.h"
#include "content/public/common/media_stream_request.h"
#include "content/public/common/result_codes.h"
#include "content/public/common/url_constants.h"
#include "content/public/common/url_utils.h"
#include "net/url_request/url_request.h"
#include "third_party/WebKit/public/platform/WebCursorInfo.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "webkit/common/resource_type.h"

#if defined(OS_MACOSX)
#include "content/browser/browser_plugin/browser_plugin_popup_menu_helper_mac.h"
#endif

namespace content {

// static
BrowserPluginHostFactory* BrowserPluginGuest::factory_ = NULL;

// Parent class for the various types of permission requests, each of which
// should be able to handle the response to their permission request.
class BrowserPluginGuest::PermissionRequest :
    public base::RefCounted<BrowserPluginGuest::PermissionRequest> {
 public:
  void Respond(bool should_allow, const std::string& user_input) {
    if (!guest_)
      return;
    RespondImpl(should_allow, user_input);
  }
  virtual bool AllowedByDefault() const {
    return false;
  }
 protected:
  explicit PermissionRequest(const base::WeakPtr<BrowserPluginGuest>& guest)
      : guest_(guest) {
    RecordAction(
        base::UserMetricsAction("BrowserPlugin.Guest.PermissionRequest"));
  }
  virtual ~PermissionRequest() {}

  virtual void RespondImpl(bool should_allow,
                           const std::string& user_input) = 0;
  // Friend RefCounted so that the dtor can be non-public.
  friend class base::RefCounted<BrowserPluginGuest::PermissionRequest>;

  base::WeakPtr<BrowserPluginGuest> guest_;
};

class BrowserPluginGuest::NewWindowRequest : public PermissionRequest {
 public:
  NewWindowRequest(const base::WeakPtr<BrowserPluginGuest>& guest,
                   int instance_id)
      : PermissionRequest(guest),
        instance_id_(instance_id) {
    RecordAction(
        base::UserMetricsAction("BrowserPlugin.Guest.PermissionRequest.NewWindow"));
  }

  virtual void RespondImpl(bool should_allow,
                           const std::string& user_input) OVERRIDE {
    int embedder_render_process_id =
        guest_->embedder_web_contents()->GetRenderProcessHost()->GetID();
    guest_->GetBrowserPluginGuestManager()->
        MaybeGetGuestByInstanceIDOrKill(
            instance_id_,
            embedder_render_process_id,
            base::Bind(&BrowserPluginGuest::NewWindowRequest::RespondInternal,
                       base::Unretained(this),
                       should_allow));
  }

 private:
  virtual ~NewWindowRequest() {}

  void RespondInternal(bool should_allow,
                       WebContents* guest_web_contents) {
    if (!guest_web_contents) {
      VLOG(0) << "Guest not found. Instance ID: " << instance_id_;
      return;
    }

    BrowserPluginGuest* guest =
        static_cast<WebContentsImpl*>(guest_web_contents)->
            GetBrowserPluginGuest();
    DCHECK(guest);
    // If we do not destroy the guest then we allow the new window.
    if (!should_allow)
      guest->Destroy();
  }

  int instance_id_;
};

namespace {
std::string WindowOpenDispositionToString(
  WindowOpenDisposition window_open_disposition) {
  switch (window_open_disposition) {
    case IGNORE_ACTION:
      return "ignore";
    case SAVE_TO_DISK:
      return "save_to_disk";
    case CURRENT_TAB:
      return "current_tab";
    case NEW_BACKGROUND_TAB:
      return "new_background_tab";
    case NEW_FOREGROUND_TAB:
      return "new_foreground_tab";
    case NEW_WINDOW:
      return "new_window";
    case NEW_POPUP:
      return "new_popup";
    default:
      NOTREACHED() << "Unknown Window Open Disposition";
      return "ignore";
  }
}

}  // namespace

class BrowserPluginGuest::EmbedderWebContentsObserver
    : public WebContentsObserver {
 public:
  explicit EmbedderWebContentsObserver(BrowserPluginGuest* guest)
      : WebContentsObserver(guest->embedder_web_contents()),
        browser_plugin_guest_(guest) {
  }

  virtual ~EmbedderWebContentsObserver() {
  }

  // WebContentsObserver:
  virtual void WebContentsDestroyed() OVERRIDE {
    browser_plugin_guest_->EmbedderDestroyed();
  }

  virtual void WasShown() OVERRIDE {
    browser_plugin_guest_->EmbedderVisibilityChanged(true);
  }

  virtual void WasHidden() OVERRIDE {
    browser_plugin_guest_->EmbedderVisibilityChanged(false);
  }

 private:
  BrowserPluginGuest* browser_plugin_guest_;

  DISALLOW_COPY_AND_ASSIGN(EmbedderWebContentsObserver);
};

BrowserPluginGuest::BrowserPluginGuest(
    int instance_id,
    bool has_render_view,
    WebContentsImpl* web_contents)
    : WebContentsObserver(web_contents),
      embedder_web_contents_(NULL),
      instance_id_(instance_id),
      guest_device_scale_factor_(1.0f),
      focused_(false),
      mouse_locked_(false),
      pending_lock_request_(false),
      guest_visible_(false),
      guest_opaque_(true),
      embedder_visible_(true),
      auto_size_enabled_(false),
      copy_request_id_(0),
      next_permission_request_id_(browser_plugin::kInvalidPermissionRequestID),
      has_render_view_(has_render_view),
      last_seen_auto_size_enabled_(false),
      is_in_destruction_(false),
      last_text_input_type_(ui::TEXT_INPUT_TYPE_NONE),
      last_input_mode_(ui::TEXT_INPUT_MODE_DEFAULT),
      last_can_compose_inline_(true),
      weak_ptr_factory_(this) {
  DCHECK(web_contents);
  web_contents->SetDelegate(this);
}

bool BrowserPluginGuest::AddMessageToConsole(WebContents* source,
                                             int32 level,
                                             const base::string16& message,
                                             int32 line_no,
                                             const base::string16& source_id) {
  if (!delegate_)
    return false;

  delegate_->AddMessageToConsole(level, message, line_no, source_id);
  return true;
}

void BrowserPluginGuest::DestroyUnattachedWindows() {
  // Destroy() reaches in and removes the BrowserPluginGuest from its opener's
  // pending_new_windows_ set. To avoid mutating the set while iterating, we
  // create a copy of the pending new windows set and iterate over the copy.
  PendingWindowMap pending_new_windows(pending_new_windows_);
  // Clean up unattached new windows opened by this guest.
  for (PendingWindowMap::const_iterator it = pending_new_windows.begin();
       it != pending_new_windows.end(); ++it) {
    it->first->Destroy();
  }
  // All pending windows should be removed from the set after Destroy() is
  // called on all of them.
  DCHECK(pending_new_windows_.empty());
}

void BrowserPluginGuest::LoadURLWithParams(const GURL& url,
                                           const Referrer& referrer,
                                           PageTransition transition_type,
                                           WebContents* web_contents) {
  NavigationController::LoadURLParams load_url_params(url);
  load_url_params.referrer = referrer;
  load_url_params.transition_type = transition_type;
  load_url_params.extra_headers = std::string();
  if (delegate_ && delegate_->IsOverridingUserAgent()) {
    load_url_params.override_user_agent =
        NavigationController::UA_OVERRIDE_TRUE;
  }
  web_contents->GetController().LoadURLWithParams(load_url_params);
}

void BrowserPluginGuest::RespondToPermissionRequest(
    int request_id,
    bool should_allow,
    const std::string& user_input) {
  RequestMap::iterator request_itr = permission_request_map_.find(request_id);
  if (request_itr == permission_request_map_.end()) {
    VLOG(0) << "Not a valid request ID.";
    return;
  }
  request_itr->second->Respond(should_allow, user_input);
  permission_request_map_.erase(request_itr);
}

void BrowserPluginGuest::RequestPermission(
    BrowserPluginPermissionType permission_type,
    scoped_refptr<BrowserPluginGuest::PermissionRequest> request,
    const base::DictionaryValue& request_info) {
  if (!delegate_) {
    // Let the stack unwind before we deny the permission request so that
    // objects held by the permission request are not destroyed immediately
    // after creation. This is to allow those same objects to be accessed again
    // in the same scope without fear of use after freeing.
    base::MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(&BrowserPluginGuest::PermissionRequest::Respond,
                   request, false, ""));
  }

  int request_id = ++next_permission_request_id_;
  permission_request_map_[request_id] = request;

  BrowserPluginGuestDelegate::PermissionResponseCallback callback =
      base::Bind(&BrowserPluginGuest::RespondToPermissionRequest,
                  AsWeakPtr(),
                  request_id);
  delegate_->RequestPermission(
      permission_type, request_info, callback, request->AllowedByDefault());
}

BrowserPluginGuest* BrowserPluginGuest::CreateNewGuestWindow(
    const OpenURLParams& params) {
  BrowserPluginGuestManager* guest_manager =
      GetBrowserPluginGuestManager();

  // Allocate a new instance ID for the new guest.
  int instance_id = guest_manager->GetNextInstanceID();

  // Set the attach params to use the same partition as the opener.
  // We pull the partition information from the site's URL, which is of the form
  // guest://site/{persist}?{partition_name}.
  const GURL& site_url = GetWebContents()->GetSiteInstance()->GetSiteURL();

  // The new guest gets a copy of this guest's extra params so that the content
  // embedder exposes the same API for this guest as its opener.
  scoped_ptr<base::DictionaryValue> extra_params(
      extra_attach_params_->DeepCopy());
  const std::string& storage_partition_id = site_url.query();
  bool persist_storage =
      site_url.path().find("persist") != std::string::npos;
  WebContents* new_guest_web_contents =
      guest_manager->CreateGuest(GetWebContents()->GetSiteInstance(),
                                 instance_id,
                                 storage_partition_id,
                                 persist_storage,
                                 extra_params.Pass());
  BrowserPluginGuest* new_guest =
      static_cast<WebContentsImpl*>(new_guest_web_contents)->
          GetBrowserPluginGuest();
  if (new_guest->delegate_)
    new_guest->delegate_->SetOpener(GetWebContents());

  // Take ownership of |new_guest|.
  pending_new_windows_.insert(
      std::make_pair(new_guest, NewWindowInfo(params.url, std::string())));

  // Request permission to show the new window.
  RequestNewWindowPermission(params.disposition, gfx::Rect(),
                             params.user_gesture, new_guest->GetWebContents());

  return new_guest;
}

base::WeakPtr<BrowserPluginGuest> BrowserPluginGuest::AsWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

bool BrowserPluginGuest::LockMouse(bool allowed) {
  if (!attached() || (mouse_locked_ == allowed))
    return false;

  return embedder_web_contents()->GotResponseToLockMouseRequest(allowed);
}

void BrowserPluginGuest::EmbedderDestroyed() {
  embedder_web_contents_ = NULL;
  if (delegate_)
    delegate_->EmbedderDestroyed();
  Destroy();
}

void BrowserPluginGuest::Destroy() {
  is_in_destruction_ = true;
  if (!attached() && GetOpener())
    GetOpener()->pending_new_windows_.erase(this);
  DestroyUnattachedWindows();
  delete GetWebContents();
}

bool BrowserPluginGuest::OnMessageReceivedFromEmbedder(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(BrowserPluginGuest, message)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_CompositorFrameSwappedACK,
                        OnCompositorFrameSwappedACK)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_CopyFromCompositingSurfaceAck,
                        OnCopyFromCompositingSurfaceAck)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_DragStatusUpdate,
                        OnDragStatusUpdate)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_ExecuteEditCommand,
                        OnExecuteEditCommand)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_ExtendSelectionAndDelete,
                        OnExtendSelectionAndDelete)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_HandleInputEvent,
                        OnHandleInputEvent)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_ImeConfirmComposition,
                        OnImeConfirmComposition)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_ImeSetComposition,
                        OnImeSetComposition)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_LockMouse_ACK, OnLockMouseAck)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_NavigateGuest, OnNavigateGuest)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_PluginDestroyed, OnPluginDestroyed)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_ReclaimCompositorResources,
                        OnReclaimCompositorResources)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_ResizeGuest, OnResizeGuest)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_SetAutoSize, OnSetSize)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_SetEditCommandsForNextKeyEvent,
                        OnSetEditCommandsForNextKeyEvent)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_SetFocus, OnSetFocus)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_SetName, OnSetName)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_SetContentsOpaque,
                        OnSetContentsOpaque)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_SetVisibility, OnSetVisibility)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_UnlockMouse_ACK, OnUnlockMouseAck)
    IPC_MESSAGE_HANDLER(BrowserPluginHostMsg_UpdateGeometry, OnUpdateGeometry)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void BrowserPluginGuest::Initialize(
    const BrowserPluginHostMsg_Attach_Params& params,
    WebContentsImpl* embedder_web_contents) {
  focused_ = params.focused;
  guest_visible_ = params.visible;
  guest_opaque_ = params.opaque;
  guest_window_rect_ = params.resize_guest_params.view_rect;

  if (!params.name.empty())
    name_ = params.name;
  auto_size_enabled_ = params.auto_size_params.enable;
  max_auto_size_ = params.auto_size_params.max_size;
  min_auto_size_ = params.auto_size_params.min_size;

  // Once a BrowserPluginGuest has an embedder WebContents, it's considered to
  // be attached.
  embedder_web_contents_ = embedder_web_contents;

  WebContentsViewGuest* new_view =
      static_cast<WebContentsViewGuest*>(GetWebContents()->GetView());
  new_view->OnGuestInitialized(embedder_web_contents->GetView());

  RendererPreferences* renderer_prefs =
      GetWebContents()->GetMutableRendererPrefs();
  std::string guest_user_agent_override = renderer_prefs->user_agent_override;
  // Copy renderer preferences (and nothing else) from the embedder's
  // WebContents to the guest.
  //
  // For GTK and Aura this is necessary to get proper renderer configuration
  // values for caret blinking interval, colors related to selection and
  // focus.
  *renderer_prefs = *embedder_web_contents_->GetMutableRendererPrefs();
  renderer_prefs->user_agent_override = guest_user_agent_override;

  // We would like the guest to report changes to frame names so that we can
  // update the BrowserPlugin's corresponding 'name' attribute.
  // TODO(fsamuel): Remove this once http://crbug.com/169110 is addressed.
  renderer_prefs->report_frame_name_changes = true;
  // Navigation is disabled in Chrome Apps. We want to make sure guest-initiated
  // navigations still continue to function inside the app.
  renderer_prefs->browser_handles_all_top_level_requests = false;
  // Disable "client blocked" error page for browser plugin.
  renderer_prefs->disable_client_blocked_error_page = true;

  embedder_web_contents_observer_.reset(new EmbedderWebContentsObserver(this));

  OnSetSize(instance_id_, params.auto_size_params, params.resize_guest_params);

  // Create a swapped out RenderView for the guest in the embedder render
  // process, so that the embedder can access the guest's window object.
  int guest_routing_id =
      GetWebContents()->CreateSwappedOutRenderView(
          embedder_web_contents_->GetSiteInstance());
  SendMessageToEmbedder(
      new BrowserPluginMsg_GuestContentWindowReady(instance_id_,
                                                   guest_routing_id));

  if (!params.src.empty()) {
    // params.src will be validated in BrowserPluginGuest::OnNavigateGuest.
    OnNavigateGuest(instance_id_, params.src);
  }

  has_render_view_ = true;

  WebPreferences prefs = GetWebContents()->GetWebkitPrefs();
  prefs.navigate_on_drag_drop = false;
  GetWebContents()->GetRenderViewHost()->UpdateWebkitPreferences(prefs);

  // Enable input method for guest if it's enabled for the embedder.
  if (static_cast<RenderViewHostImpl*>(
      embedder_web_contents_->GetRenderViewHost())->input_method_active()) {
    RenderViewHostImpl* guest_rvh = static_cast<RenderViewHostImpl*>(
        GetWebContents()->GetRenderViewHost());
    guest_rvh->SetInputMethodActive(true);
  }

  // Inform the embedder of the guest's information.
  // We pull the partition information from the site's URL, which is of the form
  // guest://site/{persist}?{partition_name}.
  const GURL& site_url = GetWebContents()->GetSiteInstance()->GetSiteURL();
  BrowserPluginMsg_Attach_ACK_Params ack_params;
  ack_params.storage_partition_id = site_url.query();
  ack_params.persist_storage =
      site_url.path().find("persist") != std::string::npos;
  ack_params.name = name_;
  SendMessageToEmbedder(
      new BrowserPluginMsg_Attach_ACK(instance_id_, ack_params));

  if (delegate_)
    delegate_->DidAttach();
}

BrowserPluginGuest::~BrowserPluginGuest() {
  while (!pending_messages_.empty()) {
    delete pending_messages_.front();
    pending_messages_.pop();
  }
}

// static
BrowserPluginGuest* BrowserPluginGuest::Create(
    int instance_id,
    SiteInstance* guest_site_instance,
    WebContentsImpl* web_contents,
    scoped_ptr<base::DictionaryValue> extra_params) {
  RecordAction(base::UserMetricsAction("BrowserPlugin.Guest.Create"));
  BrowserPluginGuest* guest = NULL;
  if (factory_) {
    guest = factory_->CreateBrowserPluginGuest(instance_id, web_contents);
  } else {
    guest = new BrowserPluginGuest(instance_id, false, web_contents);
  }
  guest->extra_attach_params_.reset(extra_params->DeepCopy());
  web_contents->SetBrowserPluginGuest(guest);
  BrowserPluginGuestDelegate* delegate = NULL;
  GetContentClient()->browser()->GuestWebContentsCreated(
      guest_site_instance, web_contents, NULL, &delegate, extra_params.Pass());
  guest->SetDelegate(delegate);
  return guest;
}

// static
BrowserPluginGuest* BrowserPluginGuest::CreateWithOpener(
    int instance_id,
    bool has_render_view,
    WebContentsImpl* web_contents,
    BrowserPluginGuest* opener) {
  BrowserPluginGuest* guest =
      new BrowserPluginGuest(
          instance_id, has_render_view, web_contents);
  web_contents->SetBrowserPluginGuest(guest);
  BrowserPluginGuestDelegate* delegate = NULL;
  GetContentClient()->browser()->GuestWebContentsCreated(
      opener->GetWebContents()->GetSiteInstance(),
      web_contents, opener->GetWebContents(), &delegate,
      scoped_ptr<base::DictionaryValue>());
  guest->SetDelegate(delegate);
  return guest;
}

RenderWidgetHostView* BrowserPluginGuest::GetEmbedderRenderWidgetHostView() {
  if (!attached())
    return NULL;
  return embedder_web_contents_->GetRenderWidgetHostView();
}

BrowserPluginGuest* BrowserPluginGuest::GetOpener() const {
  if (!delegate_)
    return NULL;

  WebContents* opener = delegate_->GetOpener();
  if (!opener)
    return NULL;

  return static_cast<WebContentsImpl*>(opener)->GetBrowserPluginGuest();
}

void BrowserPluginGuest::UpdateVisibility() {
  OnSetVisibility(instance_id_, visible());
}

void BrowserPluginGuest::CopyFromCompositingSurface(
      gfx::Rect src_subrect,
      gfx::Size dst_size,
      const base::Callback<void(bool, const SkBitmap&)>& callback) {
  copy_request_callbacks_.insert(std::make_pair(++copy_request_id_, callback));
  SendMessageToEmbedder(
      new BrowserPluginMsg_CopyFromCompositingSurface(instance_id(),
          copy_request_id_, src_subrect, dst_size));
}

BrowserPluginGuestManager*
BrowserPluginGuest::GetBrowserPluginGuestManager() const {
  return GetWebContents()->GetBrowserContext()->GetGuestManager();
}

// screen.
gfx::Rect BrowserPluginGuest::ToGuestRect(const gfx::Rect& bounds) {
  gfx::Rect guest_rect(bounds);
  guest_rect.Offset(guest_window_rect_.OffsetFromOrigin());
  return guest_rect;
}

void BrowserPluginGuest::EmbedderVisibilityChanged(bool visible) {
  embedder_visible_ = visible;
  UpdateVisibility();
}

void BrowserPluginGuest::AddNewContents(WebContents* source,
                                        WebContents* new_contents,
                                        WindowOpenDisposition disposition,
                                        const gfx::Rect& initial_pos,
                                        bool user_gesture,
                                        bool* was_blocked) {
  if (was_blocked)
    *was_blocked = false;
  RequestNewWindowPermission(disposition, initial_pos, user_gesture,
                             static_cast<WebContentsImpl*>(new_contents));
}

void BrowserPluginGuest::CanDownload(
    RenderViewHost* render_view_host,
    const GURL& url,
    const std::string& request_method,
    const base::Callback<void(bool)>& callback) {
  if (!delegate_ || !url.is_valid()) {
    callback.Run(false);
    return;
  }

  delegate_->CanDownload(request_method, url, callback);
}

void BrowserPluginGuest::LoadProgressChanged(WebContents* contents,
                                             double progress) {
  if (delegate_)
    delegate_->LoadProgressed(progress);
}

void BrowserPluginGuest::CloseContents(WebContents* source) {
  if (!delegate_)
    return;

  delegate_->Close();
}

JavaScriptDialogManager* BrowserPluginGuest::GetJavaScriptDialogManager() {
  if (!delegate_)
    return NULL;
  return delegate_->GetJavaScriptDialogManager();
}

ColorChooser* BrowserPluginGuest::OpenColorChooser(
    WebContents* web_contents,
    SkColor color,
    const std::vector<ColorSuggestion>& suggestions) {
  if (!delegate_)
    return NULL;
  return delegate_->OpenColorChooser(web_contents, color, suggestions);
}

bool BrowserPluginGuest::HandleContextMenu(const ContextMenuParams& params) {
  if (delegate_) {
    WebContentsViewGuest* view_guest =
        static_cast<WebContentsViewGuest*>(GetWebContents()->GetView());
    ContextMenuParams context_menu_params =
        view_guest->ConvertContextMenuParams(params);

    return delegate_->HandleContextMenu(context_menu_params);
  }

  // Will be handled by WebContentsViewGuest.
  return false;
}

void BrowserPluginGuest::HandleKeyboardEvent(
    WebContents* source,
    const NativeWebKeyboardEvent& event) {
  if (!delegate_)
    return;

  delegate_->HandleKeyboardEvent(event);
}

void BrowserPluginGuest::SetZoom(double zoom_factor) {
  if (delegate_)
    delegate_->SetZoom(zoom_factor);
}

void BrowserPluginGuest::PointerLockPermissionResponse(bool allow) {
  SendMessageToEmbedder(
      new BrowserPluginMsg_SetMouseLock(instance_id(), allow));
}

void BrowserPluginGuest::FindReply(WebContents* contents,
                                   int request_id,
                                   int number_of_matches,
                                   const gfx::Rect& selection_rect,
                                   int active_match_ordinal,
                                   bool final_update) {
  if (!delegate_)
    return;

  // |selection_rect| is updated to incorporate embedder coordinates.
  delegate_->FindReply(request_id, number_of_matches,
                       ToGuestRect(selection_rect),
                       active_match_ordinal, final_update);
}

WebContents* BrowserPluginGuest::OpenURLFromTab(WebContents* source,
                                                const OpenURLParams& params) {
  // If the guest wishes to navigate away prior to attachment then we save the
  // navigation to perform upon attachment. Navigation initializes a lot of
  // state that assumes an embedder exists, such as RenderWidgetHostViewGuest.
  // Navigation also resumes resource loading which we don't want to allow
  // until attachment.
  if (!attached()) {
    PendingWindowMap::iterator it =
        GetOpener()->pending_new_windows_.find(this);
    if (it == GetOpener()->pending_new_windows_.end())
      return NULL;
    const NewWindowInfo& old_target_url = it->second;
    NewWindowInfo new_window_info(params.url, old_target_url.name);
    new_window_info.changed = new_window_info.url != old_target_url.url;
    it->second = new_window_info;
    return NULL;
  }
  if (params.disposition == CURRENT_TAB) {
    // This can happen for cross-site redirects.
    LoadURLWithParams(params.url, params.referrer, params.transition, source);
    return source;
  }

  return CreateNewGuestWindow(params)->GetWebContents();
}

void BrowserPluginGuest::WebContentsCreated(WebContents* source_contents,
                                            int opener_render_frame_id,
                                            const base::string16& frame_name,
                                            const GURL& target_url,
                                            WebContents* new_contents) {
  WebContentsImpl* new_contents_impl =
      static_cast<WebContentsImpl*>(new_contents);
  BrowserPluginGuest* guest = new_contents_impl->GetBrowserPluginGuest();
  if (guest->delegate_)
    guest->delegate_->SetOpener(GetWebContents());
  std::string guest_name = base::UTF16ToUTF8(frame_name);
  guest->name_ = guest_name;
  // Take ownership of the new guest until it is attached to the embedder's DOM
  // tree to avoid leaking a guest if this guest is destroyed before attaching
  // the new guest.
  pending_new_windows_.insert(
      std::make_pair(guest, NewWindowInfo(target_url, guest_name)));
}

void BrowserPluginGuest::RendererUnresponsive(WebContents* source) {
  RecordAction(base::UserMetricsAction("BrowserPlugin.Guest.Hung"));
  if (!delegate_)
    return;
  delegate_->RendererUnresponsive();
}

void BrowserPluginGuest::RendererResponsive(WebContents* source) {
  RecordAction(base::UserMetricsAction("BrowserPlugin.Guest.Responsive"));
  if (!delegate_)
    return;
  delegate_->RendererResponsive();
}

void BrowserPluginGuest::RunFileChooser(WebContents* web_contents,
                                        const FileChooserParams& params) {
  if (!delegate_)
    return;
  delegate_->RunFileChooser(web_contents, params);
}

bool BrowserPluginGuest::ShouldFocusPageAfterCrash() {
  // Rather than managing focus in WebContentsImpl::RenderViewReady, we will
  // manage the focus ourselves.
  return false;
}

WebContentsImpl* BrowserPluginGuest::GetWebContents() const {
  return static_cast<WebContentsImpl*>(web_contents());
}

gfx::Point BrowserPluginGuest::GetScreenCoordinates(
    const gfx::Point& relative_position) const {
  gfx::Point screen_pos(relative_position);
  screen_pos += guest_window_rect_.OffsetFromOrigin();
  return screen_pos;
}

bool BrowserPluginGuest::InAutoSizeBounds(const gfx::Size& size) const {
  return size.width() <= max_auto_size_.width() &&
      size.height() <= max_auto_size_.height();
}

void BrowserPluginGuest::RequestNewWindowPermission(
    WindowOpenDisposition disposition,
    const gfx::Rect& initial_bounds,
    bool user_gesture,
    WebContentsImpl* new_contents) {
  BrowserPluginGuest* guest = new_contents->GetBrowserPluginGuest();
  PendingWindowMap::iterator it = pending_new_windows_.find(guest);
  if (it == pending_new_windows_.end())
    return;
  const NewWindowInfo& new_window_info = it->second;

  base::DictionaryValue request_info;
  request_info.Set(browser_plugin::kInitialHeight,
                   base::Value::CreateIntegerValue(initial_bounds.height()));
  request_info.Set(browser_plugin::kInitialWidth,
                   base::Value::CreateIntegerValue(initial_bounds.width()));
  request_info.Set(browser_plugin::kTargetURL,
                   base::Value::CreateStringValue(new_window_info.url.spec()));
  request_info.Set(browser_plugin::kName,
                   base::Value::CreateStringValue(new_window_info.name));
  request_info.Set(browser_plugin::kWindowID,
                   base::Value::CreateIntegerValue(guest->instance_id()));
  request_info.Set(browser_plugin::kWindowOpenDisposition,
                   base::Value::CreateStringValue(
                       WindowOpenDispositionToString(disposition)));

  RequestPermission(BROWSER_PLUGIN_PERMISSION_TYPE_NEW_WINDOW,
                    new NewWindowRequest(weak_ptr_factory_.GetWeakPtr(),
                                         guest->instance_id()),
                    request_info);
}

void BrowserPluginGuest::SendMessageToEmbedder(IPC::Message* msg) {
  if (!attached()) {
    // Some pages such as data URLs, javascript URLs, and about:blank
    // do not load external resources and so they load prior to attachment.
    // As a result, we must save all these IPCs until attachment and then
    // forward them so that the embedder gets a chance to see and process
    // the load events.
    pending_messages_.push(msg);
    return;
  }
  msg->set_routing_id(embedder_web_contents_->GetRoutingID());
  embedder_web_contents_->Send(msg);
}

void BrowserPluginGuest::DragSourceEndedAt(int client_x, int client_y,
    int screen_x, int screen_y, blink::WebDragOperation operation) {
  web_contents()->GetRenderViewHost()->DragSourceEndedAt(client_x, client_y,
      screen_x, screen_y, operation);
}

void BrowserPluginGuest::EndSystemDrag() {
  RenderViewHostImpl* guest_rvh = static_cast<RenderViewHostImpl*>(
      GetWebContents()->GetRenderViewHost());
  guest_rvh->DragSourceSystemDragEnded();
}

void BrowserPluginGuest::SetDelegate(BrowserPluginGuestDelegate* delegate) {
  DCHECK(!delegate_);
  delegate_.reset(delegate);
}

void BrowserPluginGuest::SendQueuedMessages() {
  if (!attached())
    return;

  while (!pending_messages_.empty()) {
    IPC::Message* message = pending_messages_.front();
    pending_messages_.pop();
    SendMessageToEmbedder(message);
  }
}

void BrowserPluginGuest::DidCommitProvisionalLoadForFrame(
    int64 frame_id,
    const base::string16& frame_unique_name,
    bool is_main_frame,
    const GURL& url,
    PageTransition transition_type,
    RenderViewHost* render_view_host) {
  RecordAction(base::UserMetricsAction("BrowserPlugin.Guest.DidNavigate"));
}

void BrowserPluginGuest::DidStopLoading(RenderViewHost* render_view_host) {
  bool enable_dragdrop = delegate_ && delegate_->IsDragAndDropEnabled();
  if (!enable_dragdrop) {
    // Initiating a drag from inside a guest is currently not supported without
    // the kEnableBrowserPluginDragDrop flag on a linux platform. So inject some
    // JS to disable it. http://crbug.com/161112
    const char script[] = "window.addEventListener('dragstart', function() { "
                          "  window.event.preventDefault(); "
                          "});";
    render_view_host->GetMainFrame()->ExecuteJavaScript(
        base::ASCIIToUTF16(script));
  }
}

void BrowserPluginGuest::RenderViewReady() {
  RenderViewHost* rvh = GetWebContents()->GetRenderViewHost();
  // The guest RenderView should always live in a guest process.
  CHECK(rvh->GetProcess()->IsGuest());
  // TODO(fsamuel): Investigate whether it's possible to update state earlier
  // here (see http://crbug.com/158151).
  Send(new InputMsg_SetFocus(routing_id(), focused_));
  UpdateVisibility();
  if (auto_size_enabled_)
    rvh->EnableAutoResize(min_auto_size_, max_auto_size_);
  else
    rvh->DisableAutoResize(full_size_);

  Send(new ViewMsg_SetName(routing_id(), name_));
  OnSetContentsOpaque(instance_id_, guest_opaque_);

  RenderWidgetHostImpl::From(rvh)->set_hung_renderer_delay_ms(
      base::TimeDelta::FromMilliseconds(kHungRendererDelayMs));
}

void BrowserPluginGuest::RenderProcessGone(base::TerminationStatus status) {
  SendMessageToEmbedder(new BrowserPluginMsg_GuestGone(instance_id()));
  switch (status) {
    case base::TERMINATION_STATUS_PROCESS_WAS_KILLED:
      RecordAction(base::UserMetricsAction("BrowserPlugin.Guest.Killed"));
      break;
    case base::TERMINATION_STATUS_PROCESS_CRASHED:
      RecordAction(base::UserMetricsAction("BrowserPlugin.Guest.Crashed"));
      break;
    case base::TERMINATION_STATUS_ABNORMAL_TERMINATION:
      RecordAction(
          base::UserMetricsAction("BrowserPlugin.Guest.AbnormalDeath"));
      break;
    default:
      break;
  }
  // TODO(fsamuel): Consider whether we should be clearing
  // |permission_request_map_| here.
  if (delegate_)
    delegate_->GuestProcessGone(status);
}

// static
bool BrowserPluginGuest::ShouldForwardToBrowserPluginGuest(
    const IPC::Message& message) {
  switch (message.type()) {
    case BrowserPluginHostMsg_CompositorFrameSwappedACK::ID:
    case BrowserPluginHostMsg_CopyFromCompositingSurfaceAck::ID:
    case BrowserPluginHostMsg_DragStatusUpdate::ID:
    case BrowserPluginHostMsg_ExecuteEditCommand::ID:
    case BrowserPluginHostMsg_ExtendSelectionAndDelete::ID:
    case BrowserPluginHostMsg_HandleInputEvent::ID:
    case BrowserPluginHostMsg_ImeConfirmComposition::ID:
    case BrowserPluginHostMsg_ImeSetComposition::ID:
    case BrowserPluginHostMsg_LockMouse_ACK::ID:
    case BrowserPluginHostMsg_NavigateGuest::ID:
    case BrowserPluginHostMsg_PluginDestroyed::ID:
    case BrowserPluginHostMsg_ReclaimCompositorResources::ID:
    case BrowserPluginHostMsg_ResizeGuest::ID:
    case BrowserPluginHostMsg_SetAutoSize::ID:
    case BrowserPluginHostMsg_SetEditCommandsForNextKeyEvent::ID:
    case BrowserPluginHostMsg_SetFocus::ID:
    case BrowserPluginHostMsg_SetName::ID:
    case BrowserPluginHostMsg_SetContentsOpaque::ID:
    case BrowserPluginHostMsg_SetVisibility::ID:
    case BrowserPluginHostMsg_UnlockMouse_ACK::ID:
    case BrowserPluginHostMsg_UpdateGeometry::ID:
      return true;
    default:
      return false;
  }
}

bool BrowserPluginGuest::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(BrowserPluginGuest, message)
    IPC_MESSAGE_HANDLER(ViewHostMsg_HasTouchEventHandlers,
                        OnHasTouchEventHandlers)
    IPC_MESSAGE_HANDLER(ViewHostMsg_LockMouse, OnLockMouse)
    IPC_MESSAGE_HANDLER(ViewHostMsg_SetCursor, OnSetCursor)
 #if defined(OS_MACOSX)
    // MacOSX creates and populates platform-specific select drop-down menus
    // whereas other platforms merely create a popup window that the guest
    // renderer process paints inside.
    IPC_MESSAGE_HANDLER(ViewHostMsg_ShowPopup, OnShowPopup)
 #endif
    IPC_MESSAGE_HANDLER(ViewHostMsg_ShowWidget, OnShowWidget)
    IPC_MESSAGE_HANDLER(ViewHostMsg_TakeFocus, OnTakeFocus)
    IPC_MESSAGE_HANDLER(ViewHostMsg_TextInputTypeChanged,
                        OnTextInputTypeChanged)
    IPC_MESSAGE_HANDLER(ViewHostMsg_ImeCancelComposition,
                        OnImeCancelComposition)
#if defined(OS_MACOSX) || defined(USE_AURA)
    IPC_MESSAGE_HANDLER(ViewHostMsg_ImeCompositionRangeChanged,
                        OnImeCompositionRangeChanged)
#endif
    IPC_MESSAGE_HANDLER(ViewHostMsg_UnlockMouse, OnUnlockMouse)
    IPC_MESSAGE_HANDLER(ViewHostMsg_UpdateFrameName, OnUpdateFrameName)
    IPC_MESSAGE_HANDLER(ViewHostMsg_UpdateRect, OnUpdateRect)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void BrowserPluginGuest::Attach(
    WebContentsImpl* embedder_web_contents,
    BrowserPluginHostMsg_Attach_Params params,
    const base::DictionaryValue& extra_params) {
  if (attached())
    return;

  extra_attach_params_.reset(extra_params.DeepCopy());

  // Clear parameters that get inherited from the opener.
  params.storage_partition_id.clear();
  params.persist_storage = false;
  params.src.clear();

  // If a RenderView has already been created for this new window, then we need
  // to initialize the browser-side state now so that the RenderFrameHostManager
  // does not create a new RenderView on navigation.
  if (has_render_view_) {
    static_cast<RenderViewHostImpl*>(
        GetWebContents()->GetRenderViewHost())->Init();
    WebContentsViewGuest* new_view =
        static_cast<WebContentsViewGuest*>(GetWebContents()->GetView());
    new_view->CreateViewForWidget(web_contents()->GetRenderViewHost());
  }

  // We need to do a navigation here if the target URL has changed between
  // the time the WebContents was created and the time it was attached.
  // We also need to do an initial navigation if a RenderView was never
  // created for the new window in cases where there is no referrer.
  PendingWindowMap::iterator it = GetOpener()->pending_new_windows_.find(this);
  if (it != GetOpener()->pending_new_windows_.end()) {
    const NewWindowInfo& new_window_info = it->second;
    if (new_window_info.changed || !has_render_view_)
      params.src = it->second.url.spec();
  } else {
    NOTREACHED();
  }

  // Once a new guest is attached to the DOM of the embedder page, then the
  // lifetime of the new guest is no longer managed by the opener guest.
  GetOpener()->pending_new_windows_.erase(this);

  // The guest's frame name takes precedence over the BrowserPlugin's name.
  // The guest's frame name is assigned in
  // BrowserPluginGuest::WebContentsCreated.
  if (!name_.empty())
    params.name.clear();

  Initialize(params, embedder_web_contents);

  SendQueuedMessages();

  RecordAction(base::UserMetricsAction("BrowserPlugin.Guest.Attached"));
}

void BrowserPluginGuest::OnCompositorFrameSwappedACK(
    int instance_id,
    const FrameHostMsg_CompositorFrameSwappedACK_Params& params) {
  RenderWidgetHostImpl::SendSwapCompositorFrameAck(params.producing_route_id,
                                                   params.output_surface_id,
                                                   params.producing_host_id,
                                                   params.ack);
}

void BrowserPluginGuest::OnDragStatusUpdate(int instance_id,
                                            blink::WebDragStatus drag_status,
                                            const DropData& drop_data,
                                            blink::WebDragOperationsMask mask,
                                            const gfx::Point& location) {
  RenderViewHost* host = GetWebContents()->GetRenderViewHost();
  switch (drag_status) {
    case blink::WebDragStatusEnter:
      embedder_web_contents_->GetBrowserPluginEmbedder()->DragEnteredGuest(
          this);
      host->DragTargetDragEnter(drop_data, location, location, mask, 0);
      break;
    case blink::WebDragStatusOver:
      host->DragTargetDragOver(location, location, mask, 0);
      break;
    case blink::WebDragStatusLeave:
      embedder_web_contents_->GetBrowserPluginEmbedder()->DragLeftGuest(this);
      host->DragTargetDragLeave();
      break;
    case blink::WebDragStatusDrop:
      host->DragTargetDrop(location, location, 0);
      EndSystemDrag();
      break;
    case blink::WebDragStatusUnknown:
      NOTREACHED();
  }
}

void BrowserPluginGuest::OnExecuteEditCommand(int instance_id,
                                              const std::string& name) {
  Send(new InputMsg_ExecuteEditCommand(routing_id(), name, std::string()));
}

void BrowserPluginGuest::OnImeSetComposition(
    int instance_id,
    const std::string& text,
    const std::vector<blink::WebCompositionUnderline>& underlines,
    int selection_start,
    int selection_end) {
  Send(new ViewMsg_ImeSetComposition(routing_id(),
                                     base::UTF8ToUTF16(text), underlines,
                                     selection_start, selection_end));
}

void BrowserPluginGuest::OnImeConfirmComposition(
    int instance_id,
    const std::string& text,
    bool keep_selection) {
  Send(new ViewMsg_ImeConfirmComposition(routing_id(),
                                         base::UTF8ToUTF16(text),
                                         gfx::Range::InvalidRange(),
                                         keep_selection));
}

void BrowserPluginGuest::OnExtendSelectionAndDelete(
    int instance_id,
    int before,
    int after) {
  RenderFrameHostImpl* rfh = static_cast<RenderFrameHostImpl*>(
      web_contents()->GetFocusedFrame());
  if (rfh)
    rfh->ExtendSelectionAndDelete(before, after);
}

void BrowserPluginGuest::OnReclaimCompositorResources(
    int instance_id,
    const FrameHostMsg_ReclaimCompositorResources_Params& params) {
  RenderWidgetHostImpl::SendReclaimCompositorResources(params.route_id,
                                                       params.output_surface_id,
                                                       params.renderer_host_id,
                                                       params.ack);
}

void BrowserPluginGuest::OnHandleInputEvent(
    int instance_id,
    const gfx::Rect& guest_window_rect,
    const blink::WebInputEvent* event) {
  guest_window_rect_ = guest_window_rect;
  // If the embedder's RWHV is destroyed then that means that the embedder's
  // window has been closed but the embedder's WebContents has not yet been
  // destroyed. Computing screen coordinates of a BrowserPlugin only makes sense
  // if there is a visible embedder.
  if (embedder_web_contents_->GetRenderWidgetHostView()) {
    guest_screen_rect_ = guest_window_rect;
    guest_screen_rect_.Offset(
        embedder_web_contents_->GetRenderWidgetHostView()->
            GetViewBounds().OffsetFromOrigin());
  }
  RenderViewHostImpl* guest_rvh = static_cast<RenderViewHostImpl*>(
      GetWebContents()->GetRenderViewHost());

  if (blink::WebInputEvent::isMouseEventType(event->type)) {
    guest_rvh->ForwardMouseEvent(
        *static_cast<const blink::WebMouseEvent*>(event));
    return;
  }

  if (event->type == blink::WebInputEvent::MouseWheel) {
    guest_rvh->ForwardWheelEvent(
        *static_cast<const blink::WebMouseWheelEvent*>(event));
    return;
  }

  if (blink::WebInputEvent::isKeyboardEventType(event->type)) {
    RenderViewHostImpl* embedder_rvh = static_cast<RenderViewHostImpl*>(
        embedder_web_contents_->GetRenderViewHost());
    if (!embedder_rvh->GetLastKeyboardEvent())
      return;
    NativeWebKeyboardEvent keyboard_event(
        *embedder_rvh->GetLastKeyboardEvent());
    guest_rvh->ForwardKeyboardEvent(keyboard_event);
    return;
  }

  if (blink::WebInputEvent::isTouchEventType(event->type)) {
    guest_rvh->ForwardTouchEventWithLatencyInfo(
        *static_cast<const blink::WebTouchEvent*>(event),
        ui::LatencyInfo());
    return;
  }

  if (blink::WebInputEvent::isGestureEventType(event->type)) {
    guest_rvh->ForwardGestureEvent(
        *static_cast<const blink::WebGestureEvent*>(event));
    return;
  }
}

void BrowserPluginGuest::OnLockMouse(bool user_gesture,
                                     bool last_unlocked_by_target,
                                     bool privileged) {
  if (pending_lock_request_) {
    // Immediately reject the lock because only one pointerLock may be active
    // at a time.
    Send(new ViewMsg_LockMouse_ACK(routing_id(), false));
    return;
  }

  if (!delegate_)
    return;

  pending_lock_request_ = true;

  delegate_->RequestPointerLockPermission(
      user_gesture,
      last_unlocked_by_target,
      base::Bind(&BrowserPluginGuest::PointerLockPermissionResponse,
                 weak_ptr_factory_.GetWeakPtr()));
}

void BrowserPluginGuest::OnLockMouseAck(int instance_id, bool succeeded) {
  Send(new ViewMsg_LockMouse_ACK(routing_id(), succeeded));
  pending_lock_request_ = false;
  if (succeeded)
    mouse_locked_ = true;
}

void BrowserPluginGuest::OnNavigateGuest(
    int instance_id,
    const std::string& src) {
  GURL url = delegate_ ? delegate_->ResolveURL(src) : GURL(src);

  // Do not allow navigating a guest to schemes other than known safe schemes.
  // This will block the embedder trying to load unwanted schemes, e.g.
  // chrome://settings.
  bool scheme_is_blocked =
      (!ChildProcessSecurityPolicyImpl::GetInstance()->IsWebSafeScheme(
          url.scheme()) &&
      !ChildProcessSecurityPolicyImpl::GetInstance()->IsPseudoScheme(
          url.scheme())) ||
      url.SchemeIs(kJavaScriptScheme);
  if (scheme_is_blocked || !url.is_valid()) {
    if (delegate_) {
      std::string error_type;
      base::RemoveChars(net::ErrorToString(net::ERR_ABORTED), "net::",
                        &error_type);
      delegate_->LoadAbort(true /* is_top_level */, url, error_type);
    }
    return;
  }

  GURL validated_url(url);
  GetWebContents()->GetRenderProcessHost()->FilterURL(false, &validated_url);
  // As guests do not swap processes on navigation, only navigations to
  // normal web URLs are supported.  No protocol handlers are installed for
  // other schemes (e.g., WebUI or extensions), and no permissions or bindings
  // can be granted to the guest process.
  LoadURLWithParams(validated_url, Referrer(), PAGE_TRANSITION_AUTO_TOPLEVEL,
                    GetWebContents());
}

void BrowserPluginGuest::OnPluginDestroyed(int instance_id) {
  Destroy();
}

void BrowserPluginGuest::OnResizeGuest(
    int instance_id,
    const BrowserPluginHostMsg_ResizeGuest_Params& params) {
  if (!params.size_changed)
    return;
  // BrowserPlugin manages resize flow control itself and does not depend
  // on RenderWidgetHost's mechanisms for flow control, so we reset those flags
  // here. If we are setting the size for the first time before navigating then
  // BrowserPluginGuest does not yet have a RenderViewHost.
  if (GetWebContents()->GetRenderViewHost()) {
    RenderWidgetHostImpl* render_widget_host =
        RenderWidgetHostImpl::From(GetWebContents()->GetRenderViewHost());
    render_widget_host->ResetSizeAndRepaintPendingFlags();

    if (guest_device_scale_factor_ != params.scale_factor) {
      guest_device_scale_factor_ = params.scale_factor;
      render_widget_host->NotifyScreenInfoChanged();
    }
  }
  // When autosize is turned off and as a result there is a layout change, we
  // send a sizechanged event.
  if (!auto_size_enabled_ && last_seen_auto_size_enabled_ &&
      !params.view_rect.size().IsEmpty() && delegate_) {
    delegate_->SizeChanged(last_seen_view_size_, params.view_rect.size());
    last_seen_auto_size_enabled_ = false;
  }
  // Just resize the WebContents and repaint if needed.
  full_size_ = params.view_rect.size();
  if (!params.view_rect.size().IsEmpty())
    GetWebContents()->GetView()->SizeContents(params.view_rect.size());
  if (params.repaint)
    Send(new ViewMsg_Repaint(routing_id(), params.view_rect.size()));
}

void BrowserPluginGuest::OnSetFocus(int instance_id, bool focused) {
  focused_ = focused;
  Send(new InputMsg_SetFocus(routing_id(), focused));
  if (!focused && mouse_locked_)
    OnUnlockMouse();

  // Restore the last seen state of text input to the view.
  RenderWidgetHostViewBase* rwhv = static_cast<RenderWidgetHostViewBase*>(
      web_contents()->GetRenderWidgetHostView());
  if (rwhv) {
    rwhv->TextInputTypeChanged(last_text_input_type_, last_input_mode_,
                               last_can_compose_inline_);
  }
}

void BrowserPluginGuest::OnSetName(int instance_id, const std::string& name) {
  if (name == name_)
    return;
  name_ = name;
  Send(new ViewMsg_SetName(routing_id(), name));
}

void BrowserPluginGuest::OnSetSize(
    int instance_id,
    const BrowserPluginHostMsg_AutoSize_Params& auto_size_params,
    const BrowserPluginHostMsg_ResizeGuest_Params& resize_guest_params) {
  bool old_auto_size_enabled = auto_size_enabled_;
  gfx::Size old_max_size = max_auto_size_;
  gfx::Size old_min_size = min_auto_size_;
  auto_size_enabled_ = auto_size_params.enable;
  max_auto_size_ = auto_size_params.max_size;
  min_auto_size_ = auto_size_params.min_size;
  if (auto_size_enabled_ && (!old_auto_size_enabled ||
                             (old_max_size != max_auto_size_) ||
                             (old_min_size != min_auto_size_))) {
    RecordAction(
        base::UserMetricsAction("BrowserPlugin.Guest.EnableAutoResize"));
    GetWebContents()->GetRenderViewHost()->EnableAutoResize(
        min_auto_size_, max_auto_size_);
    // TODO(fsamuel): If we're changing autosize parameters, then we force
    // the guest to completely repaint itself.
    // Ideally, we shouldn't need to do this unless |max_auto_size_| has
    // changed.
    // However, even in that case, layout may not change and so we may
    // not get a full frame worth of pixels.
    Send(new ViewMsg_Repaint(routing_id(), max_auto_size_));
  } else if (!auto_size_enabled_ && old_auto_size_enabled) {
    GetWebContents()->GetRenderViewHost()->DisableAutoResize(
        resize_guest_params.view_rect.size());
  }
  OnResizeGuest(instance_id_, resize_guest_params);
}

void BrowserPluginGuest::OnSetEditCommandsForNextKeyEvent(
    int instance_id,
    const std::vector<EditCommand>& edit_commands) {
  Send(new InputMsg_SetEditCommandsForNextKeyEvent(routing_id(),
                                                   edit_commands));
}

void BrowserPluginGuest::OnSetContentsOpaque(int instance_id, bool opaque) {
  guest_opaque_ = opaque;
  Send(new ViewMsg_SetBackgroundOpaque(routing_id(), guest_opaque_));
}

void BrowserPluginGuest::OnSetVisibility(int instance_id, bool visible) {
  guest_visible_ = visible;
  if (embedder_visible_ && guest_visible_)
    GetWebContents()->WasShown();
  else
    GetWebContents()->WasHidden();
}

void BrowserPluginGuest::OnUnlockMouse() {
  SendMessageToEmbedder(
      new BrowserPluginMsg_SetMouseLock(instance_id(), false));
}

void BrowserPluginGuest::OnUnlockMouseAck(int instance_id) {
  // mouse_locked_ could be false here if the lock attempt was cancelled due
  // to window focus, or for various other reasons before the guest was informed
  // of the lock's success.
  if (mouse_locked_)
    Send(new ViewMsg_MouseLockLost(routing_id()));
  mouse_locked_ = false;
}

void BrowserPluginGuest::OnCopyFromCompositingSurfaceAck(
    int instance_id,
    int request_id,
    const SkBitmap& bitmap) {
  CHECK(copy_request_callbacks_.count(request_id));
  if (!copy_request_callbacks_.count(request_id))
    return;
  const CopyRequestCallback& callback = copy_request_callbacks_[request_id];
  callback.Run(!bitmap.empty() && !bitmap.isNull(), bitmap);
  copy_request_callbacks_.erase(request_id);
}

void BrowserPluginGuest::OnUpdateGeometry(int instance_id,
                                          const gfx::Rect& view_rect) {
  // The plugin has moved within the embedder without resizing or the
  // embedder/container's view rect changing.
  guest_window_rect_ = view_rect;
  RenderViewHostImpl* rvh = static_cast<RenderViewHostImpl*>(
      GetWebContents()->GetRenderViewHost());
  if (rvh)
    rvh->SendScreenRects();
}

void BrowserPluginGuest::OnHasTouchEventHandlers(bool accept) {
  SendMessageToEmbedder(
      new BrowserPluginMsg_ShouldAcceptTouchEvents(instance_id(), accept));
}

void BrowserPluginGuest::OnSetCursor(const WebCursor& cursor) {
  SendMessageToEmbedder(new BrowserPluginMsg_SetCursor(instance_id(), cursor));
}

#if defined(OS_MACOSX)
void BrowserPluginGuest::OnShowPopup(
    const ViewHostMsg_ShowPopup_Params& params) {
  gfx::Rect translated_bounds(params.bounds);
  translated_bounds.Offset(guest_window_rect_.OffsetFromOrigin());
  BrowserPluginPopupMenuHelper popup_menu_helper(
      embedder_web_contents_->GetRenderViewHost(),
      GetWebContents()->GetRenderViewHost());
  popup_menu_helper.ShowPopupMenu(translated_bounds,
                                  params.item_height,
                                  params.item_font_size,
                                  params.selected_item,
                                  params.popup_items,
                                  params.right_aligned,
                                  params.allow_multiple_selection);
}
#endif

void BrowserPluginGuest::OnShowWidget(int route_id,
                                      const gfx::Rect& initial_pos) {
  GetWebContents()->ShowCreatedWidget(route_id, initial_pos);
}

void BrowserPluginGuest::OnTakeFocus(bool reverse) {
  SendMessageToEmbedder(
      new BrowserPluginMsg_AdvanceFocus(instance_id(), reverse));
}

void BrowserPluginGuest::OnUpdateFrameName(int frame_id,
                                           bool is_top_level,
                                           const std::string& name) {
  if (!is_top_level)
    return;

  name_ = name;
  SendMessageToEmbedder(new BrowserPluginMsg_UpdatedName(instance_id_, name));
}

void BrowserPluginGuest::RequestMediaAccessPermission(
    WebContents* web_contents,
    const MediaStreamRequest& request,
    const MediaResponseCallback& callback) {
  if (!delegate_) {
    callback.Run(MediaStreamDevices(),
                 MEDIA_DEVICE_INVALID_STATE,
                 scoped_ptr<MediaStreamUI>());
    return;
  }

  delegate_->RequestMediaAccessPermission(request, callback);
}

bool BrowserPluginGuest::PreHandleGestureEvent(
    WebContents* source, const blink::WebGestureEvent& event) {
  return event.type == blink::WebGestureEvent::GesturePinchBegin ||
      event.type == blink::WebGestureEvent::GesturePinchUpdate ||
      event.type == blink::WebGestureEvent::GesturePinchEnd;
}

void BrowserPluginGuest::OnUpdateRect(
    const ViewHostMsg_UpdateRect_Params& params) {
  BrowserPluginMsg_UpdateRect_Params relay_params;
  relay_params.view_size = params.view_size;
  relay_params.scale_factor = params.scale_factor;
  relay_params.is_resize_ack = ViewHostMsg_UpdateRect_Flags::is_resize_ack(
      params.flags);

  bool size_changed = last_seen_view_size_ != params.view_size;
  gfx::Size old_size = last_seen_view_size_;
  last_seen_view_size_ = params.view_size;

  if ((auto_size_enabled_ || last_seen_auto_size_enabled_) &&
      size_changed && delegate_) {
    delegate_->SizeChanged(old_size, last_seen_view_size_);
  }
  last_seen_auto_size_enabled_ = auto_size_enabled_;

  SendMessageToEmbedder(
      new BrowserPluginMsg_UpdateRect(instance_id(), relay_params));
}

void BrowserPluginGuest::OnTextInputTypeChanged(ui::TextInputType type,
                                                ui::TextInputMode input_mode,
                                                bool can_compose_inline) {
  // Save the state of text input so we can restore it on focus.
  last_text_input_type_ = type;
  last_input_mode_ = input_mode;
  last_can_compose_inline_ = can_compose_inline;

  static_cast<RenderWidgetHostViewBase*>(
      web_contents()->GetRenderWidgetHostView())->TextInputTypeChanged(
          type, input_mode, can_compose_inline);
}

void BrowserPluginGuest::OnImeCancelComposition() {
  static_cast<RenderWidgetHostViewBase*>(
      web_contents()->GetRenderWidgetHostView())->ImeCancelComposition();
}

#if defined(OS_MACOSX) || defined(USE_AURA)
void BrowserPluginGuest::OnImeCompositionRangeChanged(
      const gfx::Range& range,
      const std::vector<gfx::Rect>& character_bounds) {
  static_cast<RenderWidgetHostViewBase*>(
      web_contents()->GetRenderWidgetHostView())->ImeCompositionRangeChanged(
          range, character_bounds);
}
#endif

}  // namespace content
