// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef _MSC_VER
// Do not warn about use of std::copy with raw pointers.
#pragma warning(disable : 4996)
#endif

#include "ppapi/native_client/src/trusted/plugin/plugin.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <string>
#include <vector>

#include "native_client/src/include/nacl_base.h"
#include "native_client/src/include/nacl_macros.h"
#include "native_client/src/include/nacl_scoped_ptr.h"
#include "native_client/src/include/nacl_string.h"
#include "native_client/src/include/portability.h"
#include "native_client/src/include/portability_io.h"
#include "native_client/src/include/portability_string.h"
#include "native_client/src/shared/platform/nacl_check.h"
#include "native_client/src/trusted/desc/nacl_desc_wrapper.h"
#include "native_client/src/trusted/nonnacl_util/sel_ldr_launcher.h"
#include "native_client/src/trusted/service_runtime/nacl_error_code.h"

#include "ppapi/c/pp_errors.h"
#include "ppapi/c/ppb_var.h"
#include "ppapi/c/private/ppb_nacl_private.h"
#include "ppapi/cpp/dev/url_util_dev.h"
#include "ppapi/cpp/module.h"

#include "ppapi/native_client/src/trusted/plugin/nacl_entry_points.h"
#include "ppapi/native_client/src/trusted/plugin/nacl_subprocess.h"
#include "ppapi/native_client/src/trusted/plugin/plugin_error.h"
#include "ppapi/native_client/src/trusted/plugin/service_runtime.h"
#include "ppapi/native_client/src/trusted/plugin/utility.h"

namespace plugin {

namespace {

// Up to 20 seconds
const int64_t kTimeSmallMin = 1;         // in ms
const int64_t kTimeSmallMax = 20000;     // in ms
const uint32_t kTimeSmallBuckets = 100;

const int64_t kSizeKBMin = 1;
const int64_t kSizeKBMax = 512*1024;     // very large .nexe
const uint32_t kSizeKBBuckets = 100;

// Converts a PP_FileHandle to a POSIX file descriptor.
int32_t ConvertFileDescriptor(PP_FileHandle handle) {
  PLUGIN_PRINTF(("ConvertFileDescriptor, handle=%d\n", handle));
#if NACL_WINDOWS
  int32_t file_desc = NACL_NO_FILE_DESC;
  // On Windows, valid handles are 32 bit unsigned integers so this is safe.
  file_desc = reinterpret_cast<intptr_t>(handle);
  // Convert the Windows HANDLE from Pepper to a POSIX file descriptor.
  int32_t posix_desc = _open_osfhandle(file_desc, _O_RDWR | _O_BINARY);
  if (posix_desc == -1) {
    // Close the Windows HANDLE if it can't be converted.
    CloseHandle(reinterpret_cast<HANDLE>(file_desc));
    return -1;
  }
  return posix_desc;
#else
  return handle;
#endif
}


}  // namespace

void Plugin::ShutDownSubprocesses() {
  PLUGIN_PRINTF(("Plugin::ShutDownSubprocesses (this=%p)\n",
                 static_cast<void*>(this)));
  PLUGIN_PRINTF(("Plugin::ShutDownSubprocesses (%s)\n",
                 main_subprocess_.detailed_description().c_str()));

  // Shut down service runtime. This must be done before all other calls so
  // they don't block forever when waiting for the upcall thread to exit.
  main_subprocess_.Shutdown();

  PLUGIN_PRINTF(("Plugin::ShutDownSubprocess (this=%p, return)\n",
                 static_cast<void*>(this)));
}

void Plugin::HistogramTimeSmall(const std::string& name,
                                int64_t ms) {
  if (ms < 0) return;
  uma_interface_.HistogramCustomTimes(name,
                                      ms,
                                      kTimeSmallMin, kTimeSmallMax,
                                      kTimeSmallBuckets);
}

void Plugin::HistogramSizeKB(const std::string& name,
                             int32_t sample) {
  if (sample < 0) return;
  uma_interface_.HistogramCustomCounts(name,
                                       sample,
                                       kSizeKBMin, kSizeKBMax,
                                       kSizeKBBuckets);
}

void Plugin::HistogramEnumerateSelLdrLoadStatus(NaClErrorCode error_code) {
  if (error_code < 0 || error_code > NACL_ERROR_CODE_MAX)
    error_code = LOAD_STATUS_UNKNOWN;

  uma_interface_.HistogramEnumeration("NaCl.LoadStatus.SelLdr",
                                      error_code,
                                      NACL_ERROR_CODE_MAX);

  // Gather data to see if being installed changes load outcomes.
  const char* name = nacl_interface_->GetIsInstalled(pp_instance()) ?
      "NaCl.LoadStatus.SelLdr.InstalledApp" :
      "NaCl.LoadStatus.SelLdr.NotInstalledApp";
  uma_interface_.HistogramEnumeration(name, error_code, NACL_ERROR_CODE_MAX);
}

bool Plugin::LoadNaClModuleFromBackgroundThread(
    nacl::DescWrapper* wrapper,
    NaClSubprocess* subprocess,
    int32_t manifest_id,
    const SelLdrStartParams& params) {
  CHECK(!pp::Module::Get()->core()->IsMainThread());
  ServiceRuntime* service_runtime =
      new ServiceRuntime(this, manifest_id, false, uses_nonsfi_mode_,
                         pp::BlockUntilComplete(), pp::BlockUntilComplete());
  subprocess->set_service_runtime(service_runtime);
  PLUGIN_PRINTF(("Plugin::LoadNaClModuleFromBackgroundThread "
                 "(service_runtime=%p)\n",
                 static_cast<void*>(service_runtime)));

  // Now start the SelLdr instance.  This must be created on the main thread.
  bool service_runtime_started = false;
  pp::CompletionCallback sel_ldr_callback =
      callback_factory_.NewCallback(&Plugin::SignalStartSelLdrDone,
                                    &service_runtime_started,
                                    service_runtime);
  pp::CompletionCallback callback =
      callback_factory_.NewCallback(&Plugin::StartSelLdrOnMainThread,
                                    service_runtime, params,
                                    sel_ldr_callback);
  pp::Module::Get()->core()->CallOnMainThread(0, callback, 0);
  if (!service_runtime->WaitForSelLdrStart()) {
    PLUGIN_PRINTF(("Plugin::LoadNaClModuleFromBackgroundThread "
                   "WaitForSelLdrStart timed out!\n"));
    return false;
  }
  PLUGIN_PRINTF(("Plugin::LoadNaClModuleFromBackgroundThread "
                 "(service_runtime_started=%d)\n",
                 service_runtime_started));
  if (!service_runtime_started) {
    return false;
  }

  // Now actually load the nexe, which can happen on a background thread.
  bool nexe_loaded = service_runtime->LoadNexeAndStart(
      wrapper, pp::BlockUntilComplete());
  PLUGIN_PRINTF(("Plugin::LoadNaClModuleFromBackgroundThread "
                 "(nexe_loaded=%d)\n",
                 nexe_loaded));
  return nexe_loaded;
}

void Plugin::StartSelLdrOnMainThread(int32_t pp_error,
                                     ServiceRuntime* service_runtime,
                                     const SelLdrStartParams& params,
                                     pp::CompletionCallback callback) {
  if (pp_error != PP_OK) {
    PLUGIN_PRINTF(("Plugin::StartSelLdrOnMainThread: non-PP_OK arg "
                   "-- SHOULD NOT HAPPEN\n"));
    pp::Module::Get()->core()->CallOnMainThread(0, callback, pp_error);
    return;
  }
  service_runtime->StartSelLdr(params, callback);
}

void Plugin::SignalStartSelLdrDone(int32_t pp_error,
                                   bool* started,
                                   ServiceRuntime* service_runtime) {
  *started = (pp_error == PP_OK);
  service_runtime->SignalStartSelLdrDone();
}

void Plugin::LoadNaClModule(nacl::DescWrapper* wrapper,
                            bool uses_nonsfi_mode,
                            bool enable_dyncode_syscalls,
                            bool enable_exception_handling,
                            bool enable_crash_throttling,
                            const pp::CompletionCallback& init_done_cb,
                            const pp::CompletionCallback& crash_cb) {
  nacl::scoped_ptr<nacl::DescWrapper> scoped_wrapper(wrapper);
  CHECK(pp::Module::Get()->core()->IsMainThread());
  // Before forking a new sel_ldr process, ensure that we do not leak
  // the ServiceRuntime object for an existing subprocess, and that any
  // associated listener threads do not go unjoined because if they
  // outlive the Plugin object, they will not be memory safe.
  ShutDownSubprocesses();
  pp::Var manifest_base_url =
      pp::Var(pp::PASS_REF, nacl_interface_->GetManifestBaseURL(pp_instance()));
  std::string manifest_base_url_str = manifest_base_url.AsString();
  bool enable_dev_interfaces =
      nacl_interface_->DevInterfacesEnabled(pp_instance());
  SelLdrStartParams params(manifest_base_url_str,
                           true /* uses_irt */,
                           true /* uses_ppapi */,
                           uses_nonsfi_mode,
                           enable_dev_interfaces,
                           enable_dyncode_syscalls,
                           enable_exception_handling,
                           enable_crash_throttling);
  ErrorInfo error_info;
  ServiceRuntime* service_runtime =
      new ServiceRuntime(this, manifest_id_, true, uses_nonsfi_mode,
                         init_done_cb, crash_cb);
  main_subprocess_.set_service_runtime(service_runtime);
  PLUGIN_PRINTF(("Plugin::LoadNaClModule (service_runtime=%p)\n",
                 static_cast<void*>(service_runtime)));
  if (NULL == service_runtime) {
    error_info.SetReport(
        PP_NACL_ERROR_SEL_LDR_INIT,
        "sel_ldr init failure " + main_subprocess_.description());
    ReportLoadError(error_info);
    return;
  }

  pp::CompletionCallback callback = callback_factory_.NewCallback(
      &Plugin::LoadNexeAndStart, scoped_wrapper.release(), service_runtime,
      crash_cb);
  StartSelLdrOnMainThread(
      static_cast<int32_t>(PP_OK), service_runtime, params, callback);
}

void Plugin::LoadNexeAndStart(int32_t pp_error,
                              nacl::DescWrapper* wrapper,
                              ServiceRuntime* service_runtime,
                              const pp::CompletionCallback& crash_cb) {
  nacl::scoped_ptr<nacl::DescWrapper> scoped_wrapper(wrapper);
  if (pp_error != PP_OK)
    return;

  // Now actually load the nexe, which can happen on a background thread.
  bool nexe_loaded = service_runtime->LoadNexeAndStart(wrapper, crash_cb);
  PLUGIN_PRINTF(("Plugin::LoadNaClModule (nexe_loaded=%d)\n",
                 nexe_loaded));
  if (nexe_loaded) {
    PLUGIN_PRINTF(("Plugin::LoadNaClModule (%s)\n",
                   main_subprocess_.detailed_description().c_str()));
  }
}

bool Plugin::LoadNaClModuleContinuationIntern() {
  ErrorInfo error_info;
  if (!uses_nonsfi_mode_) {
    if (!main_subprocess_.StartSrpcServices()) {
      // The NaCl process probably crashed. On Linux, a crash causes this
      // error, while on other platforms, the error is detected below, when we
      // attempt to start the proxy. Report a module initialization error here,
      // to make it less confusing for developers.
      NaClLog(LOG_ERROR, "LoadNaClModuleContinuationIntern: "
              "StartSrpcServices failed\n");
      error_info.SetReport(PP_NACL_ERROR_START_PROXY_MODULE,
                           "could not initialize module.");
      ReportLoadError(error_info);
      return false;
    }
  }

  bool result = PP_ToBool(nacl_interface_->StartPpapiProxy(pp_instance()));
  if (result) {
    PLUGIN_PRINTF(("Plugin::LoadNaClModule (%s)\n",
                   main_subprocess_.detailed_description().c_str()));
  }
  return result;
}

NaClSubprocess* Plugin::LoadHelperNaClModule(const nacl::string& helper_url,
                                             nacl::DescWrapper* wrapper,
                                             int32_t manifest_id,
                                             ErrorInfo* error_info) {
  nacl::scoped_ptr<NaClSubprocess> nacl_subprocess(
      new NaClSubprocess("helper module", NULL, NULL));
  if (NULL == nacl_subprocess.get()) {
    error_info->SetReport(PP_NACL_ERROR_SEL_LDR_INIT,
                          "unable to allocate helper subprocess.");
    return NULL;
  }

  // Do not report UMA stats for translator-related nexes.
  // TODO(sehr): define new UMA stats for translator related nexe events.
  // NOTE: The PNaCl translator nexes are not built to use the IRT.  This is
  // done to save on address space and swap space.
  // TODO(jvoung): See if we still need the uses_ppapi variable, now that
  // LaunchSelLdr always happens on the main thread.
  bool enable_dev_interfaces =
      nacl_interface_->DevInterfacesEnabled(pp_instance());
  SelLdrStartParams params(helper_url,
                           false /* uses_irt */,
                           false /* uses_ppapi */,
                           false /* uses_nonsfi_mode */,
                           enable_dev_interfaces,
                           false /* enable_dyncode_syscalls */,
                           false /* enable_exception_handling */,
                           true /* enable_crash_throttling */);
  if (!LoadNaClModuleFromBackgroundThread(wrapper, nacl_subprocess.get(),
                                          manifest_id, params)) {
    return NULL;
  }
  // We need not wait for the init_done callback.  We can block
  // here in StartSrpcServices, since helper NaCl modules
  // are spawned from a private thread.
  //
  // TODO(bsy): if helper module crashes, we should abort.
  // crash_cb is not used here, so we are relying on crashes
  // being detected in StartSrpcServices or later.
  //
  // NB: More refactoring might be needed, however, if helper
  // NaCl modules have their own manifest.  Currently the
  // manifest is a per-plugin-instance object, not a per
  // NaClSubprocess object.
  if (!nacl_subprocess->StartSrpcServices()) {
    error_info->SetReport(PP_NACL_ERROR_SRPC_CONNECTION_FAIL,
                          "SRPC connection failure for " +
                          nacl_subprocess->description());
    return NULL;
  }

  PLUGIN_PRINTF(("Plugin::LoadHelperNaClModule (%s, %s)\n",
                 helper_url.c_str(),
                 nacl_subprocess.get()->detailed_description().c_str()));

  return nacl_subprocess.release();
}

// All failures of this function will show up as "Missing Plugin-in", so
// there is no need to log to JS console that there was an initialization
// failure. Note that module loading functions will log their own errors.
bool Plugin::Init(uint32_t argc, const char* argn[], const char* argv[]) {
  PLUGIN_PRINTF(("Plugin::Init (argc=%" NACL_PRIu32 ")\n", argc));
  nacl_interface_->InitializePlugin(pp_instance(), argc, argn, argv);
  wrapper_factory_ = new nacl::DescWrapperFactory();
  pp::Var manifest_url(pp::PASS_REF, nacl_interface_->GetManifestURLArgument(
      pp_instance()));
  if (manifest_url.is_string() && !manifest_url.AsString().empty())
    RequestNaClManifest(manifest_url.AsString());
  return true;
}

Plugin::Plugin(PP_Instance pp_instance)
    : pp::Instance(pp_instance),
      main_subprocess_("main subprocess", NULL, NULL),
      uses_nonsfi_mode_(false),
      wrapper_factory_(NULL),
      time_of_last_progress_event_(0),
      manifest_id_(-1),
      nexe_handle_(PP_kInvalidFileHandle),
      nacl_interface_(NULL),
      uma_interface_(this) {
  PLUGIN_PRINTF(("Plugin::Plugin (this=%p, pp_instance=%"
                 NACL_PRId32 ")\n", static_cast<void*>(this), pp_instance));
  callback_factory_.Initialize(this);
  nacl_interface_ = GetNaClInterface();
  CHECK(nacl_interface_ != NULL);

  // Notify PPB_NaCl_Private that the instance is created before altering any
  // state that it tracks.
  nacl_interface_->InstanceCreated(pp_instance);
  // We call set_exit_status() here to ensure that the 'exitStatus' property is
  // set. This can only be called when nacl_interface_ is not NULL.
  set_exit_status(-1);
}


Plugin::~Plugin() {
  int64_t shutdown_start = NaClGetTimeOfDayMicroseconds();

  PLUGIN_PRINTF(("Plugin::~Plugin (this=%p)\n",
                 static_cast<void*>(this)));
  // Destroy the coordinator while the rest of the data is still there
  pnacl_coordinator_.reset(NULL);

  for (std::map<nacl::string, NaClFileInfoAutoCloser*>::iterator it =
           url_file_info_map_.begin();
       it != url_file_info_map_.end();
       ++it) {
    delete it->second;
  }
  url_downloaders_.erase(url_downloaders_.begin(), url_downloaders_.end());

  // Clean up accounting for our instance inside the NaCl interface.
  nacl_interface_->InstanceDestroyed(pp_instance());

  // ShutDownSubprocesses shuts down the main subprocess, which shuts
  // down the main ServiceRuntime object, which kills the subprocess.
  // As a side effect of the subprocess being killed, the reverse
  // services thread(s) will get EOF on the reverse channel(s), and
  // the thread(s) will exit.  In ServiceRuntime::Shutdown, we invoke
  // ReverseService::WaitForServiceThreadsToExit(), so that there will
  // not be an extent thread(s) hanging around.  This means that the
  // ~Plugin will block until this happens.  This is a requirement,
  // since the renderer should be free to unload the plugin code, and
  // we cannot have threads running code that gets unloaded before
  // they exit.
  //
  // By waiting for the threads here, we also ensure that the Plugin
  // object and the subprocess and ServiceRuntime objects is not
  // (fully) destroyed while the threads are running, so resources
  // that are destroyed after ShutDownSubprocesses (below) are
  // guaranteed to be live and valid for access from the service
  // threads.
  //
  // The main_subprocess object, which wraps the main service_runtime
  // object, is dtor'd implicitly after the explicit code below runs,
  // so the main service runtime object will not have been dtor'd,
  // though the Shutdown method may have been called, during the
  // lifetime of the service threads.
  ShutDownSubprocesses();

  delete wrapper_factory_;

  HistogramTimeSmall(
      "NaCl.Perf.ShutdownTime.Total",
      (NaClGetTimeOfDayMicroseconds() - shutdown_start)
          / NACL_MICROS_PER_MILLI);

  PLUGIN_PRINTF(("Plugin::~Plugin (this=%p, return)\n",
                 static_cast<void*>(this)));
}

bool Plugin::HandleDocumentLoad(const pp::URLLoader& url_loader) {
  PLUGIN_PRINTF(("Plugin::HandleDocumentLoad (this=%p)\n",
                 static_cast<void*>(this)));
  // We don't know if the plugin will handle the document load, but return
  // true in order to give it a chance to respond once the proxy is started.
  return true;
}

void Plugin::NexeFileDidOpen(int32_t pp_error) {
  if (pp_error != PP_OK)
    return;

  int32_t desc = ConvertFileDescriptor(nexe_handle_);
  nexe_handle_ = PP_kInvalidFileHandle;  // Clear out nexe handle.

  nacl::scoped_ptr<nacl::DescWrapper>
      wrapper(wrapper_factory()->MakeFileDesc(desc, O_RDONLY));
  NaClLog(4, "NexeFileDidOpen: invoking LoadNaClModule\n");
  LoadNaClModule(
      wrapper.release(),
      uses_nonsfi_mode_,
      true, /* enable_dyncode_syscalls */
      true, /* enable_exception_handling */
      false, /* enable_crash_throttling */
      callback_factory_.NewCallback(&Plugin::NexeFileDidOpenContinuation),
      callback_factory_.NewCallback(&Plugin::NexeDidCrash));
}

void Plugin::NexeFileDidOpenContinuation(int32_t pp_error) {
  bool was_successful;

  UNREFERENCED_PARAMETER(pp_error);
  NaClLog(4, "Entered NexeFileDidOpenContinuation\n");
  NaClLog(4, "NexeFileDidOpenContinuation: invoking"
          " LoadNaClModuleContinuationIntern\n");
  was_successful = LoadNaClModuleContinuationIntern();
  if (was_successful) {
    NaClLog(4, "NexeFileDidOpenContinuation: success;"
            " setting histograms\n");
    int64_t nexe_size = nacl_interface_->GetNexeSize(pp_instance());
    ReportLoadSuccess(nexe_size, nexe_size);
  } else {
    NaClLog(4, "NexeFileDidOpenContinuation: failed.");
  }
  NaClLog(4, "Leaving NexeFileDidOpenContinuation\n");
}

void Plugin::NexeDidCrash(int32_t pp_error) {
  PLUGIN_PRINTF(("Plugin::NexeDidCrash (pp_error=%" NACL_PRId32 ")\n",
                 pp_error));
  if (pp_error != PP_OK) {
    PLUGIN_PRINTF(("Plugin::NexeDidCrash: CallOnMainThread callback with"
                   " non-PP_OK arg -- SHOULD NOT HAPPEN\n"));
  }

  std::string crash_log =
      main_subprocess_.service_runtime()->GetCrashLogOutput();
  nacl_interface_->NexeDidCrash(pp_instance(), crash_log.c_str());
}

void Plugin::BitcodeDidTranslate(int32_t pp_error) {
  PLUGIN_PRINTF(("Plugin::BitcodeDidTranslate (pp_error=%" NACL_PRId32 ")\n",
                 pp_error));
  if (pp_error != PP_OK) {
    // Error should have been reported by pnacl. Just return.
    PLUGIN_PRINTF(("Plugin::BitcodeDidTranslate error in Pnacl\n"));
    return;
  }

  // Inform JavaScript that we successfully translated the bitcode to a nexe.
  nacl::scoped_ptr<nacl::DescWrapper>
      wrapper(pnacl_coordinator_.get()->ReleaseTranslatedFD());
  LoadNaClModule(
      wrapper.release(),
      false, /* uses_nonsfi_mode */
      false, /* enable_dyncode_syscalls */
      false, /* enable_exception_handling */
      true, /* enable_crash_throttling */
      callback_factory_.NewCallback(&Plugin::BitcodeDidTranslateContinuation),
      callback_factory_.NewCallback(&Plugin::NexeDidCrash));
}

void Plugin::BitcodeDidTranslateContinuation(int32_t pp_error) {
  bool was_successful = LoadNaClModuleContinuationIntern();

  NaClLog(4, "Entered BitcodeDidTranslateContinuation\n");
  UNREFERENCED_PARAMETER(pp_error);
  if (was_successful) {
    int64_t loaded;
    int64_t total;
    pnacl_coordinator_->GetCurrentProgress(&loaded, &total);
    ReportLoadSuccess(loaded, total);
  }
}

void Plugin::NaClManifestFileDidOpen(int32_t pp_error) {
  PLUGIN_PRINTF(("Plugin::NaClManifestFileDidOpen (pp_error=%"
                 NACL_PRId32 ")\n", pp_error));
  if (pp_error == PP_OK) {
    // Take local ownership of manifest_data_var_
    pp::Var manifest_data = pp::Var(pp::PASS_REF, manifest_data_var_);
    manifest_data_var_ = PP_MakeUndefined();

    std::string json_buffer = manifest_data.AsString();
    ProcessNaClManifest(json_buffer);
  }
}

void Plugin::ProcessNaClManifest(const nacl::string& manifest_json) {
  HistogramSizeKB("NaCl.Perf.Size.Manifest",
                  static_cast<int32_t>(manifest_json.length() / 1024));
  if (!SetManifestObject(manifest_json))
    return;

  PP_Var pp_program_url;
  PP_PNaClOptions pnacl_options = {PP_FALSE, PP_FALSE, 2};
  PP_Bool uses_nonsfi_mode;
  if (nacl_interface_->GetManifestProgramURL(pp_instance(),
          manifest_id_, &pp_program_url, &pnacl_options, &uses_nonsfi_mode)) {
    program_url_ = pp::Var(pp::PASS_REF, pp_program_url).AsString();
    // TODO(teravest): Make ProcessNaClManifest take responsibility for more of
    // this function.
    nacl_interface_->ProcessNaClManifest(pp_instance(), program_url_.c_str());
    uses_nonsfi_mode_ = PP_ToBool(uses_nonsfi_mode);
    if (pnacl_options.translate) {
      pp::CompletionCallback translate_callback =
          callback_factory_.NewCallback(&Plugin::BitcodeDidTranslate);
      pnacl_coordinator_.reset(
          PnaclCoordinator::BitcodeToNative(this,
                                            program_url_,
                                            pnacl_options,
                                            translate_callback));
      return;
    } else {
      pp::CompletionCallback open_callback =
          callback_factory_.NewCallback(&Plugin::NexeFileDidOpen);
      // Will always call the callback on success or failure.
      nacl_interface_->DownloadNexe(pp_instance(),
                                    program_url_.c_str(),
                                    &nexe_handle_,
                                    open_callback.pp_completion_callback());
      return;
    }
  }
}

void Plugin::RequestNaClManifest(const nacl::string& url) {
  PLUGIN_PRINTF(("Plugin::RequestNaClManifest (url='%s')\n", url.c_str()));
  pp::CompletionCallback open_callback =
      callback_factory_.NewCallback(&Plugin::NaClManifestFileDidOpen);
  nacl_interface_->RequestNaClManifest(pp_instance(),
                                       url.c_str(),
                                       &manifest_data_var_,
                                       open_callback.pp_completion_callback());
}


bool Plugin::SetManifestObject(const nacl::string& manifest_json) {
  PLUGIN_PRINTF(("Plugin::SetManifestObject(): manifest_json='%s'.\n",
       manifest_json.c_str()));
  // Determine whether lookups should use portable (i.e., pnacl versions)
  // rather than platform-specific files.
  pp::Var manifest_base_url =
      pp::Var(pp::PASS_REF, nacl_interface_->GetManifestBaseURL(pp_instance()));
  std::string manifest_base_url_str = manifest_base_url.AsString();

  int32_t manifest_id = nacl_interface_->CreateJsonManifest(
      pp_instance(),
      manifest_base_url_str.c_str(),
      manifest_json.c_str());
  if (manifest_id == -1)
    return false;
  manifest_id_ = manifest_id;
  return true;
}

void Plugin::UrlDidOpenForStreamAsFile(
    int32_t pp_error,
    FileDownloader* url_downloader,
    pp::CompletionCallback callback) {
  PLUGIN_PRINTF(("Plugin::UrlDidOpen (pp_error=%" NACL_PRId32
                 ", url_downloader=%p)\n", pp_error,
                 static_cast<void*>(url_downloader)));
  url_downloaders_.erase(url_downloader);
  nacl::scoped_ptr<FileDownloader> scoped_url_downloader(url_downloader);
  NaClFileInfo tmp_info(scoped_url_downloader->GetFileInfo());
  NaClFileInfoAutoCloser *info = new NaClFileInfoAutoCloser(&tmp_info);

  if (pp_error != PP_OK) {
    callback.Run(pp_error);
    delete info;
  } else if (info->get_desc() > NACL_NO_FILE_DESC) {
    std::map<nacl::string, NaClFileInfoAutoCloser*>::iterator it =
        url_file_info_map_.find(url_downloader->url());
    if (it != url_file_info_map_.end())
      delete it->second;
    url_file_info_map_[url_downloader->url()] = info;
    callback.Run(PP_OK);
  } else {
    callback.Run(PP_ERROR_FAILED);
    delete info;
  }
}

struct NaClFileInfo Plugin::GetFileInfo(const nacl::string& url) {
  struct NaClFileInfo info;
  memset(&info, 0, sizeof(info));
  std::map<nacl::string, NaClFileInfoAutoCloser*>::iterator it =
      url_file_info_map_.find(url);
  if (it != url_file_info_map_.end()) {
    info = it->second->get();
    info.desc = DUP(info.desc);
  } else {
    info.desc = -1;
  }
  return info;
}

bool Plugin::StreamAsFile(const nacl::string& url,
                          const pp::CompletionCallback& callback) {
  PLUGIN_PRINTF(("Plugin::StreamAsFile (url='%s')\n", url.c_str()));
  FileDownloader* downloader = new FileDownloader();
  downloader->Initialize(this);
  url_downloaders_.insert(downloader);

  // Untrusted loads are always relative to the page's origin.
  if (!GetNaClInterface()->ResolvesRelativeToPluginBaseUrl(pp_instance(),
                                                           url.c_str()))
    return false;

  // Try the fast path first. This will only block if the file is installed.
  if (OpenURLFast(url, downloader)) {
    UrlDidOpenForStreamAsFile(PP_OK, downloader, callback);
    return true;
  }

  pp::CompletionCallback open_callback = callback_factory_.NewCallback(
      &Plugin::UrlDidOpenForStreamAsFile, downloader, callback);
  // If true, will always call the callback on success or failure.
  return downloader->Open(url,
                          DOWNLOAD_TO_FILE,
                          open_callback,
                          true,
                          &UpdateDownloadProgress);
}


void Plugin::ReportLoadSuccess(uint64_t loaded_bytes, uint64_t total_bytes) {
  nacl_interface_->ReportLoadSuccess(
      pp_instance(), program_url_.c_str(), loaded_bytes, total_bytes);
}


void Plugin::ReportLoadError(const ErrorInfo& error_info) {
  nacl_interface_->ReportLoadError(pp_instance(),
                                   error_info.error_code(),
                                   error_info.message().c_str(),
                                   error_info.console_message().c_str());
}


void Plugin::ReportLoadAbort() {
  nacl_interface_->ReportLoadAbort(pp_instance());
}

void Plugin::UpdateDownloadProgress(
    PP_Instance pp_instance,
    PP_Resource pp_resource,
    int64_t /*bytes_sent*/,
    int64_t /*total_bytes_to_be_sent*/,
    int64_t bytes_received,
    int64_t total_bytes_to_be_received) {
  Instance* instance = pp::Module::Get()->InstanceForPPInstance(pp_instance);
  if (instance != NULL) {
    Plugin* plugin = static_cast<Plugin*>(instance);
    // Rate limit progress events to a maximum of 100 per second.
    int64_t time = NaClGetTimeOfDayMicroseconds();
    int64_t elapsed = time - plugin->time_of_last_progress_event_;
    const int64_t kTenMilliseconds = 10000;
    if (elapsed > kTenMilliseconds) {
      plugin->time_of_last_progress_event_ = time;

      // Find the URL loader that sent this notification.
      const FileDownloader* file_downloader =
          plugin->FindFileDownloader(pp_resource);
      nacl::string url;
      if (file_downloader)
        url = file_downloader->url();
      LengthComputable length_computable = (total_bytes_to_be_received >= 0) ?
          LENGTH_IS_COMPUTABLE : LENGTH_IS_NOT_COMPUTABLE;

      plugin->EnqueueProgressEvent(PP_NACL_EVENT_PROGRESS,
                                   url,
                                   length_computable,
                                   bytes_received,
                                   total_bytes_to_be_received);
    }
  }
}

const FileDownloader* Plugin::FindFileDownloader(
    PP_Resource url_loader) const {
  const FileDownloader* file_downloader = NULL;
  std::set<FileDownloader*>::const_iterator it = url_downloaders_.begin();
  while (it != url_downloaders_.end()) {
    if (url_loader == (*it)->url_loader()) {
      file_downloader = (*it);
      break;
    }
    ++it;
  }
  return file_downloader;
}

void Plugin::ReportSelLdrLoadStatus(int status) {
  HistogramEnumerateSelLdrLoadStatus(static_cast<NaClErrorCode>(status));
}

void Plugin::EnqueueProgressEvent(PP_NaClEventType event_type,
                                  const nacl::string& url,
                                  LengthComputable length_computable,
                                  uint64_t loaded_bytes,
                                  uint64_t total_bytes) {
  PLUGIN_PRINTF(("Plugin::EnqueueProgressEvent ("
                 "event_type='%d', url='%s', length_computable=%d, "
                 "loaded=%" NACL_PRIu64 ", total=%" NACL_PRIu64 ")\n",
                 static_cast<int>(event_type),
                 url.c_str(),
                 static_cast<int>(length_computable),
                 loaded_bytes,
                 total_bytes));

  nacl_interface_->DispatchEvent(
      pp_instance(),
      event_type,
      url.c_str(),
      length_computable == LENGTH_IS_COMPUTABLE ? PP_TRUE : PP_FALSE,
      loaded_bytes,
      total_bytes);
}

bool Plugin::OpenURLFast(const nacl::string& url,
                         FileDownloader* downloader) {
  uint64_t file_token_lo = 0;
  uint64_t file_token_hi = 0;
  PP_FileHandle file_handle =
      nacl_interface()->OpenNaClExecutable(pp_instance(),
                                           url.c_str(),
                                           &file_token_lo, &file_token_hi);
  // We shouldn't hit this if the file URL is in an installed app.
  if (file_handle == PP_kInvalidFileHandle)
    return false;

  // FileDownloader takes ownership of the file handle.
  downloader->OpenFast(url, file_handle, file_token_lo, file_token_hi);
  return true;
}

bool Plugin::DocumentCanRequest(const std::string& url) {
  CHECK(pp::Module::Get()->core()->IsMainThread());
  CHECK(pp::URLUtil_Dev::Get() != NULL);
  return pp::URLUtil_Dev::Get()->DocumentCanRequest(this, pp::Var(url));
}

void Plugin::set_exit_status(int exit_status) {
  pp::Core* core = pp::Module::Get()->core();
  if (core->IsMainThread()) {
    SetExitStatusOnMainThread(PP_OK, exit_status);
  } else {
    pp::CompletionCallback callback =
        callback_factory_.NewCallback(&Plugin::SetExitStatusOnMainThread,
                                      exit_status);
    core->CallOnMainThread(0, callback, 0);
  }
}

void Plugin::SetExitStatusOnMainThread(int32_t pp_error,
                                       int exit_status) {
  DCHECK(pp::Module::Get()->core()->IsMainThread());
  DCHECK(nacl_interface_);
  nacl_interface_->SetExitStatus(pp_instance(), exit_status);
}


}  // namespace plugin
