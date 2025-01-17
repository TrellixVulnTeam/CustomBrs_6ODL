// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/managed/managed_user_test_base.h"

#include <string>

#include "base/compiler_specific.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/sequenced_worker_pool.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/login/login_manager_test.h"
#include "chrome/browser/chromeos/login/managed/supervised_user_authentication.h"
#include "chrome/browser/chromeos/login/startup_utils.h"
#include "chrome/browser/chromeos/login/ui/login_display_host_impl.h"
#include "chrome/browser/chromeos/login/ui/webui_login_view.h"
#include "chrome/browser/chromeos/login/users/supervised_user_manager.h"
#include "chrome/browser/chromeos/net/network_portal_detector_test_impl.h"
#include "chrome/browser/chromeos/settings/stub_cros_settings_provider.h"
#include "chrome/browser/managed_mode/managed_user_constants.h"
#include "chrome/browser/managed_mode/managed_user_registration_utility.h"
#include "chrome/browser/managed_mode/managed_user_registration_utility_stub.h"
#include "chrome/browser/managed_mode/managed_user_shared_settings_service.h"
#include "chrome/browser/managed_mode/managed_user_shared_settings_service_factory.h"
#include "chrome/browser/managed_mode/managed_user_sync_service.h"
#include "chrome/browser/managed_mode/managed_user_sync_service_factory.h"
#include "chrome/browser/profiles/profile_impl.h"
#include "chromeos/cryptohome/mock_async_method_caller.h"
#include "chromeos/cryptohome/mock_homedir_methods.h"
#include "content/public/browser/notification_service.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "sync/api/attachments/attachment_service_proxy_for_test.h"
#include "sync/api/fake_sync_change_processor.h"
#include "sync/api/sync_change.h"
#include "sync/api/sync_error_factory_mock.h"
#include "sync/protocol/sync.pb.h"

using testing::_;
using base::StringPrintf;

namespace chromeos {

namespace {

const char kCurrentPage[] = "$('managed-user-creation').currentPage_";
}

ManagedUsersSyncTestAdapter::ManagedUsersSyncTestAdapter(Profile* profile)
    : processor_(), next_sync_data_id_(0) {
  service_ = ManagedUserSyncServiceFactory::GetForProfile(profile);
  processor_ = new syncer::FakeSyncChangeProcessor();
  service_->MergeDataAndStartSyncing(
      syncer::MANAGED_USERS,
      syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(processor_),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock));
}

scoped_ptr< ::sync_pb::ManagedUserSpecifics>
ManagedUsersSyncTestAdapter::GetFirstChange() {
  scoped_ptr< ::sync_pb::ManagedUserSpecifics> result(
      new ::sync_pb::ManagedUserSpecifics);
  CHECK(HasChanges())
      << "GetFirstChange() should only be callled if HasChanges() is true";
  const syncer::SyncData& data = processor_->changes().front().sync_data();
  EXPECT_EQ(syncer::MANAGED_USERS, data.GetDataType());
  result->CopyFrom(data.GetSpecifics().managed_user());
  return result.Pass();
}

void ManagedUsersSyncTestAdapter::AddChange(
    const ::sync_pb::ManagedUserSpecifics& proto,
    bool update) {
  sync_pb::EntitySpecifics specifics;

  specifics.mutable_managed_user()->CopyFrom(proto);

  syncer::SyncData change_data = syncer::SyncData::CreateRemoteData(
      ++next_sync_data_id_,
      specifics,
      base::Time(),
      syncer::AttachmentIdList(),
      syncer::AttachmentServiceProxyForTest::Create());
  syncer::SyncChange change(FROM_HERE,
                            update ? syncer::SyncChange::ACTION_UPDATE
                                   : syncer::SyncChange::ACTION_ADD,
                            change_data);

  syncer::SyncChangeList change_list;
  change_list.push_back(change);

  service_->ProcessSyncChanges(FROM_HERE, change_list);
}

ManagedUsersSharedSettingsSyncTestAdapter::
    ManagedUsersSharedSettingsSyncTestAdapter(Profile* profile)
    : processor_(), next_sync_data_id_(0) {
  service_ =
      ManagedUserSharedSettingsServiceFactory::GetForBrowserContext(profile);
  processor_ = new syncer::FakeSyncChangeProcessor();
  service_->MergeDataAndStartSyncing(
      syncer::MANAGED_USER_SHARED_SETTINGS,
      syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(processor_),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock));
}

scoped_ptr< ::sync_pb::ManagedUserSharedSettingSpecifics>
ManagedUsersSharedSettingsSyncTestAdapter::GetFirstChange() {
  scoped_ptr< ::sync_pb::ManagedUserSharedSettingSpecifics> result(
      new ::sync_pb::ManagedUserSharedSettingSpecifics);
  CHECK(HasChanges())
      << "GetFirstChange() should only be callled if HasChanges() is true";
  const syncer::SyncData& data = processor_->changes().front().sync_data();
  EXPECT_EQ(syncer::MANAGED_USER_SHARED_SETTINGS, data.GetDataType());
  result->CopyFrom(data.GetSpecifics().managed_user_shared_setting());
  return result.Pass();
}

void ManagedUsersSharedSettingsSyncTestAdapter::AddChange(
    const ::sync_pb::ManagedUserSharedSettingSpecifics& proto,
    bool update) {
  sync_pb::EntitySpecifics specifics;

  specifics.mutable_managed_user_shared_setting()->CopyFrom(proto);

  syncer::SyncData change_data = syncer::SyncData::CreateRemoteData(
      ++next_sync_data_id_,
      specifics,
      base::Time(),
      syncer::AttachmentIdList(),
      syncer::AttachmentServiceProxyForTest::Create());
  syncer::SyncChange change(FROM_HERE,
                            update ? syncer::SyncChange::ACTION_UPDATE
                                   : syncer::SyncChange::ACTION_ADD,
                            change_data);

  syncer::SyncChangeList change_list;
  change_list.push_back(change);

  service_->ProcessSyncChanges(FROM_HERE, change_list);
}

void ManagedUsersSharedSettingsSyncTestAdapter::AddChange(
    const std::string& mu_id,
    const std::string& key,
    const base::Value& value,
    bool acknowledged,
    bool update) {
  syncer::SyncData data =
      ManagedUserSharedSettingsService::CreateSyncDataForSetting(
          mu_id, key, value, acknowledged);
  AddChange(data.GetSpecifics().managed_user_shared_setting(), update);
}

ManagedUserTestBase::ManagedUserTestBase()
    : LoginManagerTest(true),
      mock_async_method_caller_(NULL),
      mock_homedir_methods_(NULL),
      network_portal_detector_(NULL),
      registration_utility_stub_(NULL) {
}

ManagedUserTestBase::~ManagedUserTestBase() {
}

void ManagedUserTestBase::SetUpInProcessBrowserTestFixture() {
  LoginManagerTest::SetUpInProcessBrowserTestFixture();
  mock_async_method_caller_ = new cryptohome::MockAsyncMethodCaller;
  mock_async_method_caller_->SetUp(true, cryptohome::MOUNT_ERROR_NONE);
  cryptohome::AsyncMethodCaller::InitializeForTesting(
      mock_async_method_caller_);

  mock_homedir_methods_ = new cryptohome::MockHomedirMethods;
  mock_homedir_methods_->SetUp(true, cryptohome::MOUNT_ERROR_NONE);
  cryptohome::HomedirMethods::InitializeForTesting(mock_homedir_methods_);

  registration_utility_stub_ = new ManagedUserRegistrationUtilityStub();
  scoped_utility_.reset(new ScopedTestingManagedUserRegistrationUtility(
      registration_utility_stub_));

  // Setup network portal detector to return online state for both
  // ethernet and wifi networks. Ethernet is an active network by
  // default.
  network_portal_detector_ = new NetworkPortalDetectorTestImpl();
  NetworkPortalDetector::InitializeForTesting(network_portal_detector_);
  NetworkPortalDetector::CaptivePortalState online_state;
  online_state.status = NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_ONLINE;
  online_state.response_code = 204;
  network_portal_detector_->SetDefaultNetworkPathForTesting(
      kStubEthernetServicePath);
  network_portal_detector_->SetDetectionResultsForTesting(
      kStubEthernetServicePath, online_state);
}

void ManagedUserTestBase::CleanUpOnMainThread() {
  LoginManagerTest::CleanUpOnMainThread();
}

void ManagedUserTestBase::TearDown() {
  cryptohome::AsyncMethodCaller::Shutdown();
  cryptohome::HomedirMethods::Shutdown();
  mock_homedir_methods_ = NULL;
  mock_async_method_caller_ = NULL;
  LoginManagerTest::TearDown();
}

void ManagedUserTestBase::TearDownInProcessBrowserTestFixture() {
  NetworkPortalDetector::Shutdown();
}

void ManagedUserTestBase::JSEval(const std::string& script) {
  EXPECT_TRUE(content::ExecuteScript(web_contents(), script));
}

void ManagedUserTestBase::JSExpectAsync(const std::string& function) {
  bool result;
  EXPECT_TRUE(content::ExecuteScriptAndExtractBool(
      web_contents(),
      StringPrintf(
          "(%s)(function() { window.domAutomationController.send(true); });",
          function.c_str()),
      &result));
  EXPECT_TRUE(result);
}

void ManagedUserTestBase::JSSetTextField(const std::string& element_selector,
                                         const std::string& value) {
  std::string function =
      StringPrintf("document.querySelector('%s').value = '%s'",
                   element_selector.c_str(),
                   value.c_str());
  JSEval(function);
}

void ManagedUserTestBase::PrepareUsers() {
  RegisterUser(kTestManager);
  RegisterUser(kTestOtherUser);
  chromeos::StartupUtils::MarkOobeCompleted();
}

void ManagedUserTestBase::StartFlowLoginAsManager() {
  // Navigate to supervised user creation screen.
  JSEval("chrome.send('showLocallyManagedUserCreationScreen')");

  // Read intro and proceed.
  JSExpect(StringPrintf("%s == 'intro'", kCurrentPage));

  JSEval("$('managed-user-creation-start-button').click()");

  // Check that both users appear as managers, and test-manager@gmail.com is
  // the first one.
  JSExpect(StringPrintf("%s == 'manager'", kCurrentPage));

  std::string manager_pods =
      "document.querySelectorAll('#managed-user-creation-managers-pane "
      ".manager-pod')";
  std::string selected_manager_pods =
      "document.querySelectorAll('#managed-user-creation-managers-pane "
      ".manager-pod.focused')";

  int managers_on_device = 2;

  JSExpect(StringPrintf("%s.length == 1", selected_manager_pods.c_str()));

  JSExpect(
      StringPrintf("$('managed-user-creation').managerList_.pods.length == %d",
                   managers_on_device));
  JSExpect(StringPrintf(
      "%s.length == %d", manager_pods.c_str(), managers_on_device));
  JSExpect(StringPrintf("%s[%d].user.emailAddress == '%s'",
                        manager_pods.c_str(),
                        0,
                        kTestManager));

  // Select the first user as manager, and enter password.
  JSExpect("$('managed-user-creation-next-button').disabled");
  JSSetTextField("#managed-user-creation .manager-pod.focused input",
                 kTestManagerPassword);

  JSEval("$('managed-user-creation').updateNextButtonForManager_()");

  // Next button is now enabled.
  JSExpect("!$('managed-user-creation-next-button').disabled");
  SetExpectedCredentials(kTestManager, kTestManagerPassword);
  content::WindowedNotificationObserver login_observer(
      chrome::NOTIFICATION_LOGIN_USER_PROFILE_PREPARED,
      content::NotificationService::AllSources());

  // Log in as manager.
  JSEval("$('managed-user-creation-next-button').click()");
  login_observer.Wait();

  // OAuth token is valid.
  UserManager::Get()->SaveUserOAuthStatus(kTestManager,
                                          User::OAUTH2_TOKEN_STATUS_VALID);
  base::RunLoop().RunUntilIdle();

  // Check the page have changed.
  JSExpect(StringPrintf("%s == 'username'", kCurrentPage));
}

void ManagedUserTestBase::FillNewUserData(const std::string& display_name) {
  JSExpect("$('managed-user-creation-next-button').disabled");
  JSSetTextField("#managed-user-creation-name", display_name);
  JSEval("$('managed-user-creation').checkUserName_()");

  base::RunLoop().RunUntilIdle();

  JSSetTextField("#managed-user-creation-password",
                 kTestSupervisedUserPassword);
  JSSetTextField("#managed-user-creation-password-confirm",
                 kTestSupervisedUserPassword);

  JSEval("$('managed-user-creation').updateNextButtonForUser_()");
  JSExpect("!$('managed-user-creation-next-button').disabled");
}

void ManagedUserTestBase::StartUserCreation(
    const std::string& button_id,
    const std::string& expected_display_name) {
  EXPECT_CALL(*mock_homedir_methods_, MountEx(_, _, _, _)).Times(1);
  EXPECT_CALL(*mock_homedir_methods_, AddKeyEx(_, _, _, _, _)).Times(1);

  JSEval(std::string("$('").append(button_id).append("').click()"));

  ::testing::Mock::VerifyAndClearExpectations(mock_homedir_methods_);

  EXPECT_TRUE(registration_utility_stub_->register_was_called());
  EXPECT_EQ(registration_utility_stub_->display_name(),
            base::UTF8ToUTF16(expected_display_name));

  registration_utility_stub_->RunSuccessCallback("token");

  // Token writing moves control to BlockingPool and back.
  base::RunLoop().RunUntilIdle();
  content::BrowserThread::GetBlockingPool()->FlushForTesting();
  base::RunLoop().RunUntilIdle();

  JSExpect(StringPrintf("%s == 'created'", kCurrentPage));
  JSEval("$('managed-user-creation-gotit-button').click()");
}

void ManagedUserTestBase::SigninAsSupervisedUser(
    bool check_homedir_calls,
    int user_index,
    const std::string& expected_display_name) {
  if (check_homedir_calls)
    EXPECT_CALL(*mock_homedir_methods_, MountEx(_, _, _, _)).Times(1);

  // Log in as supervised user, make sure that everything works.
  ASSERT_EQ(3UL, UserManager::Get()->GetUsers().size());

  // Created supervised user have to be first in a list.
  const User* user = UserManager::Get()->GetUsers().at(user_index);
  ASSERT_EQ(base::UTF8ToUTF16(expected_display_name), user->display_name());
  LoginUser(user->email());
  if (check_homedir_calls)
    ::testing::Mock::VerifyAndClearExpectations(mock_homedir_methods_);
  Profile* profile = UserManager::Get()->GetProfileByUser(user);
  shared_settings_adapter_.reset(
      new ManagedUsersSharedSettingsSyncTestAdapter(profile));

  // Check ChromeOS preference is initialized.
  EXPECT_TRUE(
      static_cast<ProfileImpl*>(profile)->chromeos_preferences_);
}

void ManagedUserTestBase::SigninAsManager(int user_index) {
  // Log in as supervised user, make sure that everything works.
  ASSERT_EQ(3UL, UserManager::Get()->GetUsers().size());

  // Created supervised user have to be first in a list.
  const User* user = UserManager::Get()->GetUsers().at(user_index);
  LoginUser(user->email());
  Profile* profile = UserManager::Get()->GetProfileByUser(user);
  shared_settings_adapter_.reset(
      new ManagedUsersSharedSettingsSyncTestAdapter(profile));
  managed_users_adapter_.reset(new ManagedUsersSyncTestAdapter(profile));
}

void ManagedUserTestBase::RemoveSupervisedUser(
    unsigned long original_user_count,
    int user_index,
    const std::string& expected_display_name) {
  // Remove supervised user.
  ASSERT_EQ(original_user_count, UserManager::Get()->GetUsers().size());

  // Created supervised user have to be first in a list.
  const User* user = UserManager::Get()->GetUsers().at(user_index);
  ASSERT_EQ(base::UTF8ToUTF16(expected_display_name), user->display_name());

  // Open pod menu.
  JSExpect(
      StringPrintf("!$('pod-row').pods[%d].isActionBoxMenuActive", user_index));
  JSEval(StringPrintf(
      "$('pod-row').pods[%d].querySelector('.action-box-button').click()",
      user_index));
  JSExpect(
      StringPrintf("$('pod-row').pods[%d].isActionBoxMenuActive", user_index));

  // Select "Remove user" element.
  JSExpect(StringPrintf(
      "$('pod-row').pods[%d].actionBoxRemoveUserWarningElement.hidden",
      user_index));
  JSEval(StringPrintf(
      "$('pod-row').pods[%d].querySelector('.action-box-menu-remove').click()",
      user_index));
  JSExpect(StringPrintf(
      "!$('pod-row').pods[%d].actionBoxRemoveUserWarningElement.hidden",
      user_index));

  EXPECT_CALL(*mock_async_method_caller_, AsyncRemove(_, _)).Times(1);

  // Confirm deletion.
  JSEval(StringPrintf(
      "$('pod-row').pods[%d].querySelector('.remove-warning-button').click()",
      user_index));

  // Make sure there is no supervised user in list.
  ASSERT_EQ(original_user_count - 1, UserManager::Get()->GetUsers().size());
}

}  // namespace chromeos
