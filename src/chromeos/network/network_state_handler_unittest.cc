// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/network_state_handler.h"

#include <map>
#include <set>
#include <string>

#include "base/bind.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/values.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/shill_device_client.h"
#include "chromeos/dbus/shill_manager_client.h"
#include "chromeos/dbus/shill_profile_client.h"
#include "chromeos/dbus/shill_service_client.h"
#include "chromeos/network/favorite_state.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler_observer.h"
#include "chromeos/network/shill_property_util.h"
#include "dbus/object_path.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace {

void ErrorCallbackFunction(const std::string& error_name,
                           const std::string& error_message) {
  LOG(ERROR) << "Shill Error: " << error_name << " : " << error_message;
}

const std::string kShillManagerClientStubDefaultService = "eth1";
const std::string kShillManagerClientStubDefaultWifi = "wifi1";
const std::string kShillManagerClientStubWifi2 = "wifi2";
const std::string kShillManagerClientStubCellular = "cellular1";

using chromeos::NetworkState;
using chromeos::NetworkStateHandler;

class TestObserver : public chromeos::NetworkStateHandlerObserver {
 public:
  explicit TestObserver(NetworkStateHandler* handler)
      : handler_(handler),
        device_list_changed_count_(0),
        network_count_(0),
        default_network_change_count_(0),
        favorite_count_(0) {
  }

  virtual ~TestObserver() {
  }

  virtual void DeviceListChanged() OVERRIDE {
    ++device_list_changed_count_;
  }

  virtual void NetworkListChanged() OVERRIDE {
    NetworkStateHandler::NetworkStateList networks;
    handler_->GetNetworkList(&networks);
    network_count_ = networks.size();
    if (network_count_ == 0) {
      default_network_ = "";
      default_network_connection_state_ = "";
    }
    NetworkStateHandler::FavoriteStateList favorites;
    handler_->GetFavoriteList(&favorites);
    favorite_count_ = favorites.size();
  }

  virtual void DefaultNetworkChanged(const NetworkState* network) OVERRIDE {
    ++default_network_change_count_;
    default_network_ = network ? network->path() : "";
    default_network_connection_state_ =
        network ? network->connection_state() : "";
    DVLOG(1) << "DefaultNetworkChanged: " << default_network_
             << " State: " << default_network_connection_state_;
  }

  virtual void NetworkConnectionStateChanged(
      const NetworkState* network) OVERRIDE {
    network_connection_state_[network->path()] = network->connection_state();
    connection_state_changes_[network->path()]++;
  }

  virtual void NetworkPropertiesUpdated(const NetworkState* network) OVERRIDE {
    DCHECK(network);
    property_updates_[network->path()]++;
  }

  size_t device_list_changed_count() { return device_list_changed_count_; }
  size_t network_count() { return network_count_; }
  size_t default_network_change_count() {
    return default_network_change_count_;
  }
  void reset_network_change_count() {
    DVLOG(1) << "ResetNetworkChangeCount";
    default_network_change_count_ = 0;
  }
  std::string default_network() { return default_network_; }
  std::string default_network_connection_state() {
    return default_network_connection_state_;
  }
  size_t favorite_count() { return favorite_count_; }

  int PropertyUpdatesForService(const std::string& service_path) {
    return property_updates_[service_path];
  }

  int ConnectionStateChangesForService(const std::string& service_path) {
    return connection_state_changes_[service_path];
  }

  std::string NetworkConnectionStateForService(
      const std::string& service_path) {
    return network_connection_state_[service_path];
  }

 private:
  NetworkStateHandler* handler_;
  size_t device_list_changed_count_;
  size_t network_count_;
  size_t default_network_change_count_;
  std::string default_network_;
  std::string default_network_connection_state_;
  size_t favorite_count_;
  std::map<std::string, int> property_updates_;
  std::map<std::string, int> connection_state_changes_;
  std::map<std::string, std::string> network_connection_state_;

  DISALLOW_COPY_AND_ASSIGN(TestObserver);
};

}  // namespace

namespace chromeos {

class NetworkStateHandlerTest : public testing::Test {
 public:
  NetworkStateHandlerTest()
      : device_test_(NULL),
        manager_test_(NULL),
        profile_test_(NULL),
        service_test_(NULL) {}
  virtual ~NetworkStateHandlerTest() {}

  virtual void SetUp() OVERRIDE {
    // Initialize DBusThreadManager with a stub implementation.
    DBusThreadManager::InitializeWithStub();
    SetupNetworkStateHandler();
    message_loop_.RunUntilIdle();
  }

  virtual void TearDown() OVERRIDE {
    network_state_handler_->RemoveObserver(test_observer_.get(), FROM_HERE);
    test_observer_.reset();
    network_state_handler_.reset();
    DBusThreadManager::Shutdown();
  }

  void SetupNetworkStateHandler() {
    SetupDefaultShillState();
    network_state_handler_.reset(new NetworkStateHandler);
    test_observer_.reset(new TestObserver(network_state_handler_.get()));
    network_state_handler_->AddObserver(test_observer_.get(), FROM_HERE);
    network_state_handler_->InitShillPropertyHandler();
  }

 protected:
  void AddService(const std::string& service_path,
                  const std::string& name,
                  const std::string& type,
                  const std::string& state) {
    service_test_->AddService(service_path, name, type, state,
                              true /* add_to_visible */,
                              true /* add_to_watchlist */);
  }

  void SetupDefaultShillState() {
    message_loop_.RunUntilIdle();  // Process any pending updates
    device_test_ =
        DBusThreadManager::Get()->GetShillDeviceClient()->GetTestInterface();
    ASSERT_TRUE(device_test_);
    device_test_->ClearDevices();
    device_test_->AddDevice(
        "/device/stub_wifi_device1", shill::kTypeWifi, "stub_wifi_device1");
    device_test_->AddDevice("/device/stub_cellular_device1",
                            shill::kTypeCellular,
                            "stub_cellular_device1");

    manager_test_ =
        DBusThreadManager::Get()->GetShillManagerClient()->GetTestInterface();
    ASSERT_TRUE(manager_test_);

    profile_test_ =
        DBusThreadManager::Get()->GetShillProfileClient()->GetTestInterface();
    ASSERT_TRUE(profile_test_);
    profile_test_->ClearProfiles();

    service_test_ =
        DBusThreadManager::Get()->GetShillServiceClient()->GetTestInterface();
    ASSERT_TRUE(service_test_);
    service_test_->ClearServices();
    AddService(kShillManagerClientStubDefaultService,
               kShillManagerClientStubDefaultService,
               shill::kTypeEthernet,
               shill::kStateOnline);
    AddService(kShillManagerClientStubDefaultWifi,
               kShillManagerClientStubDefaultWifi,
               shill::kTypeWifi,
               shill::kStateOnline);
    AddService(kShillManagerClientStubWifi2,
               kShillManagerClientStubWifi2,
               shill::kTypeWifi,
               shill::kStateIdle);
    AddService(kShillManagerClientStubCellular,
               kShillManagerClientStubCellular,
               shill::kTypeCellular,
               shill::kStateIdle);
  }

  void UpdateManagerProperties() {
    message_loop_.RunUntilIdle();
    network_state_handler_->UpdateManagerProperties();
    message_loop_.RunUntilIdle();
  }

  base::MessageLoopForUI message_loop_;
  scoped_ptr<NetworkStateHandler> network_state_handler_;
  scoped_ptr<TestObserver> test_observer_;
  ShillDeviceClient::TestInterface* device_test_;
  ShillManagerClient::TestInterface* manager_test_;
  ShillProfileClient::TestInterface* profile_test_;
  ShillServiceClient::TestInterface* service_test_;

 private:
  DISALLOW_COPY_AND_ASSIGN(NetworkStateHandlerTest);
};

TEST_F(NetworkStateHandlerTest, NetworkStateHandlerStub) {
  // Ensure that the network list is the expected size.
  const size_t kNumShillManagerClientStubImplServices = 4;
  EXPECT_EQ(kNumShillManagerClientStubImplServices,
            test_observer_->network_count());
  // Ensure that the first stub network is the default network.
  EXPECT_EQ(kShillManagerClientStubDefaultService,
            test_observer_->default_network());
  EXPECT_EQ(kShillManagerClientStubDefaultService,
            network_state_handler_->ConnectedNetworkByType(
                NetworkTypePattern::Default())->path());
  EXPECT_EQ(kShillManagerClientStubDefaultService,
            network_state_handler_->ConnectedNetworkByType(
                NetworkTypePattern::Ethernet())->path());
  EXPECT_EQ(kShillManagerClientStubDefaultWifi,
            network_state_handler_->ConnectedNetworkByType(
                NetworkTypePattern::WiFi())->path());
  EXPECT_EQ(kShillManagerClientStubCellular,
            network_state_handler_->FirstNetworkByType(
                NetworkTypePattern::Mobile())->path());
  EXPECT_EQ(
      kShillManagerClientStubCellular,
      network_state_handler_->FirstNetworkByType(NetworkTypePattern::Cellular())
          ->path());
  EXPECT_EQ(shill::kStateOnline,
            test_observer_->default_network_connection_state());
}

TEST_F(NetworkStateHandlerTest, TechnologyChanged) {
  // There may be several manager changes during initialization.
  size_t initial_changed_count = test_observer_->device_list_changed_count();
  // Disable a technology.
  network_state_handler_->SetTechnologyEnabled(
      NetworkTypePattern::Wimax(), false, network_handler::ErrorCallback());
  EXPECT_NE(
      NetworkStateHandler::TECHNOLOGY_ENABLED,
      network_state_handler_->GetTechnologyState(NetworkTypePattern::Wimax()));
  EXPECT_EQ(initial_changed_count + 1,
            test_observer_->device_list_changed_count());
  // Enable a technology.
  network_state_handler_->SetTechnologyEnabled(
      NetworkTypePattern::Wimax(), true, network_handler::ErrorCallback());
  // The technology state should immediately change to ENABLING and we should
  // receive a manager changed callback.
  EXPECT_EQ(initial_changed_count + 2,
            test_observer_->device_list_changed_count());
  EXPECT_EQ(
      NetworkStateHandler::TECHNOLOGY_ENABLING,
      network_state_handler_->GetTechnologyState(NetworkTypePattern::Wimax()));
  message_loop_.RunUntilIdle();
  // Ensure we receive 2 manager changed callbacks when the technology becomes
  // avalable and enabled.
  EXPECT_EQ(initial_changed_count + 4,
            test_observer_->device_list_changed_count());
  EXPECT_EQ(
      NetworkStateHandler::TECHNOLOGY_ENABLED,
      network_state_handler_->GetTechnologyState(NetworkTypePattern::Wimax()));
}

TEST_F(NetworkStateHandlerTest, TechnologyState) {
  manager_test_->RemoveTechnology(shill::kTypeWimax);
  message_loop_.RunUntilIdle();
  EXPECT_EQ(
      NetworkStateHandler::TECHNOLOGY_UNAVAILABLE,
      network_state_handler_->GetTechnologyState(NetworkTypePattern::Wimax()));

  manager_test_->AddTechnology(shill::kTypeWimax, false);
  message_loop_.RunUntilIdle();
  EXPECT_EQ(
      NetworkStateHandler::TECHNOLOGY_AVAILABLE,
      network_state_handler_->GetTechnologyState(NetworkTypePattern::Wimax()));

  manager_test_->SetTechnologyInitializing(shill::kTypeWimax, true);
  message_loop_.RunUntilIdle();
  EXPECT_EQ(
      NetworkStateHandler::TECHNOLOGY_UNINITIALIZED,
      network_state_handler_->GetTechnologyState(NetworkTypePattern::Wimax()));

  manager_test_->SetTechnologyInitializing(shill::kTypeWimax, false);
  network_state_handler_->SetTechnologyEnabled(
      NetworkTypePattern::Wimax(), true, network_handler::ErrorCallback());
  message_loop_.RunUntilIdle();
  EXPECT_EQ(
      NetworkStateHandler::TECHNOLOGY_ENABLED,
      network_state_handler_->GetTechnologyState(NetworkTypePattern::Wimax()));

  manager_test_->RemoveTechnology(shill::kTypeWimax);
  message_loop_.RunUntilIdle();
  EXPECT_EQ(
      NetworkStateHandler::TECHNOLOGY_UNAVAILABLE,
      network_state_handler_->GetTechnologyState(NetworkTypePattern::Wimax()));
}

TEST_F(NetworkStateHandlerTest, ServicePropertyChanged) {
  // Set a service property.
  const std::string eth1 = kShillManagerClientStubDefaultService;
  const NetworkState* ethernet = network_state_handler_->GetNetworkState(eth1);
  ASSERT_TRUE(ethernet);
  EXPECT_EQ("", ethernet->security());
  EXPECT_EQ(1, test_observer_->PropertyUpdatesForService(eth1));
  base::StringValue security_value("TestSecurity");
  DBusThreadManager::Get()->GetShillServiceClient()->SetProperty(
      dbus::ObjectPath(eth1),
      shill::kSecurityProperty, security_value,
      base::Bind(&base::DoNothing), base::Bind(&ErrorCallbackFunction));
  message_loop_.RunUntilIdle();
  ethernet = network_state_handler_->GetNetworkState(eth1);
  EXPECT_EQ("TestSecurity", ethernet->security());
  EXPECT_EQ(2, test_observer_->PropertyUpdatesForService(eth1));

  // Changing a service to the existing value should not trigger an update.
  DBusThreadManager::Get()->GetShillServiceClient()->SetProperty(
      dbus::ObjectPath(eth1),
      shill::kSecurityProperty, security_value,
      base::Bind(&base::DoNothing), base::Bind(&ErrorCallbackFunction));
  message_loop_.RunUntilIdle();
  EXPECT_EQ(2, test_observer_->PropertyUpdatesForService(eth1));
}

TEST_F(NetworkStateHandlerTest, GetState) {
  const std::string profile = "/profile/profile1";
  const std::string wifi_path = kShillManagerClientStubDefaultWifi;

  // Add a wifi service to a Profile.
  profile_test_->AddProfile(profile, "" /* userhash */);
  EXPECT_TRUE(profile_test_->AddService(profile, wifi_path));
  UpdateManagerProperties();

  // Ensure that a NetworkState and corresponding FavoriteState exist.
  const NetworkState* wifi_network =
      network_state_handler_->GetNetworkState(wifi_path);
  ASSERT_TRUE(wifi_network);
  const FavoriteState* wifi_favorite =
      network_state_handler_->GetFavoriteStateFromServicePath(
          wifi_path, true /* configured_only */);
  ASSERT_TRUE(wifi_favorite);
  EXPECT_EQ(wifi_network->path(), wifi_favorite->path());

  // Ensure that we are notified that a Favorite was added.
  EXPECT_EQ(1u, test_observer_->favorite_count());

  // Test looking up by GUID.
  ASSERT_FALSE(wifi_favorite->guid().empty());
  const FavoriteState* wifi_favorite_guid =
      network_state_handler_->GetFavoriteStateFromGuid(wifi_favorite->guid());
  EXPECT_EQ(wifi_favorite, wifi_favorite_guid);

  // Remove the service, verify that there is no longer a NetworkState for it.
  service_test_->RemoveService(wifi_path);
  UpdateManagerProperties();
  EXPECT_FALSE(network_state_handler_->GetNetworkState(wifi_path));
}

TEST_F(NetworkStateHandlerTest, NetworkConnectionStateChanged) {
  // Change a network state.
  const std::string eth1 = kShillManagerClientStubDefaultService;
  base::StringValue connection_state_idle_value(shill::kStateIdle);
  service_test_->SetServiceProperty(eth1, shill::kStateProperty,
                                   connection_state_idle_value);
  message_loop_.RunUntilIdle();
  EXPECT_EQ(shill::kStateIdle,
            test_observer_->NetworkConnectionStateForService(eth1));
  EXPECT_EQ(2, test_observer_->ConnectionStateChangesForService(eth1));
  // Confirm that changing the connection state to the same value does *not*
  // signal the observer.
  service_test_->SetServiceProperty(eth1, shill::kStateProperty,
                                   connection_state_idle_value);
  message_loop_.RunUntilIdle();
  EXPECT_EQ(2, test_observer_->ConnectionStateChangesForService(eth1));
}

TEST_F(NetworkStateHandlerTest, DefaultServiceDisconnected) {
  const std::string eth1 = kShillManagerClientStubDefaultService;
  const std::string wifi1 = kShillManagerClientStubDefaultWifi;

  // Disconnect ethernet.
  test_observer_->reset_network_change_count();
  base::StringValue connection_state_idle_value(shill::kStateIdle);
  service_test_->SetServiceProperty(eth1, shill::kStateProperty,
                                    connection_state_idle_value);
  message_loop_.RunUntilIdle();
  // Expect two changes: first when eth1 becomes disconnected, second when
  // wifi1 becomes the default.
  EXPECT_EQ(2u, test_observer_->default_network_change_count());
  EXPECT_EQ(wifi1, test_observer_->default_network());

  // Disconnect wifi.
  test_observer_->reset_network_change_count();
  service_test_->SetServiceProperty(wifi1, shill::kStateProperty,
                                    connection_state_idle_value);
  message_loop_.RunUntilIdle();
  EXPECT_EQ(1u, test_observer_->default_network_change_count());
  EXPECT_EQ("", test_observer_->default_network());
}

TEST_F(NetworkStateHandlerTest, DefaultServiceConnected) {
  const std::string eth1 = kShillManagerClientStubDefaultService;
  const std::string wifi1 = kShillManagerClientStubDefaultWifi;

  // Disconnect ethernet and wifi.
  base::StringValue connection_state_idle_value(shill::kStateIdle);
  service_test_->SetServiceProperty(eth1, shill::kStateProperty,
                                    connection_state_idle_value);
  service_test_->SetServiceProperty(wifi1, shill::kStateProperty,
                                    connection_state_idle_value);
  message_loop_.RunUntilIdle();
  EXPECT_EQ(std::string(), test_observer_->default_network());

  // Connect ethernet, should become the default network.
  test_observer_->reset_network_change_count();
  base::StringValue connection_state_ready_value(shill::kStateReady);
  service_test_->SetServiceProperty(eth1, shill::kStateProperty,
                                    connection_state_ready_value);
  message_loop_.RunUntilIdle();
  EXPECT_EQ(eth1, test_observer_->default_network());
  EXPECT_EQ(shill::kStateReady,
            test_observer_->default_network_connection_state());
  EXPECT_EQ(1u, test_observer_->default_network_change_count());
}

TEST_F(NetworkStateHandlerTest, DefaultServiceChanged) {
  const std::string eth1 = kShillManagerClientStubDefaultService;
  // The default service should be eth1.
  EXPECT_EQ(eth1, test_observer_->default_network());

  // Change the default network by changing Manager.DefaultService.
  test_observer_->reset_network_change_count();
  const std::string wifi1 = kShillManagerClientStubDefaultWifi;
  base::StringValue wifi1_value(wifi1);
  manager_test_->SetManagerProperty(
      shill::kDefaultServiceProperty, wifi1_value);
  message_loop_.RunUntilIdle();
  EXPECT_EQ(wifi1, test_observer_->default_network());
  EXPECT_EQ(1u, test_observer_->default_network_change_count());

  // Change the state of the default network.
  test_observer_->reset_network_change_count();
  base::StringValue connection_state_ready_value(shill::kStateReady);
  service_test_->SetServiceProperty(wifi1, shill::kStateProperty,
                                   connection_state_ready_value);
  message_loop_.RunUntilIdle();
  EXPECT_EQ(shill::kStateReady,
            test_observer_->default_network_connection_state());
  EXPECT_EQ(1u, test_observer_->default_network_change_count());

  // Updating a property on the default network should trigger
  // a default network change.
  test_observer_->reset_network_change_count();
  DBusThreadManager::Get()->GetShillServiceClient()->SetProperty(
      dbus::ObjectPath(wifi1),
      shill::kSecurityProperty, base::StringValue("TestSecurity"),
      base::Bind(&base::DoNothing), base::Bind(&ErrorCallbackFunction));
  message_loop_.RunUntilIdle();
  EXPECT_EQ(1u, test_observer_->default_network_change_count());

  // No default network updates for signal strength changes.
  test_observer_->reset_network_change_count();
  DBusThreadManager::Get()->GetShillServiceClient()->SetProperty(
      dbus::ObjectPath(wifi1),
      shill::kSignalStrengthProperty, base::FundamentalValue(32),
      base::Bind(&base::DoNothing), base::Bind(&ErrorCallbackFunction));
  message_loop_.RunUntilIdle();
  EXPECT_EQ(0u, test_observer_->default_network_change_count());
}

TEST_F(NetworkStateHandlerTest, RequestUpdate) {
  // Request an update for kShillManagerClientStubDefaultWifi.
  EXPECT_EQ(1, test_observer_->PropertyUpdatesForService(
      kShillManagerClientStubDefaultWifi));
  network_state_handler_->RequestUpdateForNetwork(
      kShillManagerClientStubDefaultWifi);
  message_loop_.RunUntilIdle();
  EXPECT_EQ(2, test_observer_->PropertyUpdatesForService(
      kShillManagerClientStubDefaultWifi));
}

TEST_F(NetworkStateHandlerTest, NetworkGuidInProfile) {
  const std::string profile = "/profile/profile1";
  const std::string wifi_path = "wifi_with_guid";
  const std::string wifi_guid = "WIFI_GUID";
  const bool is_service_configured = true;

  // Add a network to the default Profile with a specified GUID.
  service_test_->AddServiceWithIPConfig(
      wifi_path,
      wifi_guid,
      wifi_path  /* name */,
      shill::kTypeWifi,
      shill::kStateOnline,
      "" /* ipconfig_path */,
      true /* add_to_visible */,
      true /* add_to_watchlist */);
  profile_test_->AddProfile(profile, "" /* userhash */);
  EXPECT_TRUE(profile_test_->AddService(profile, wifi_path));
  UpdateManagerProperties();

  // Verify that a FavoriteState exists with a matching GUID.
  const FavoriteState* favorite =
      network_state_handler_->GetFavoriteStateFromServicePath(
          wifi_path, is_service_configured);
  ASSERT_TRUE(favorite);
  EXPECT_EQ(wifi_guid, favorite->guid());

  // Verify that a NetworkState exists with the same GUID.
  const NetworkState* network =
      network_state_handler_->GetNetworkState(wifi_path);
  ASSERT_TRUE(network);
  EXPECT_EQ(wifi_guid, network->guid());

  // Remove the service (simulating a network going out of range).
  service_test_->RemoveService(wifi_path);
  UpdateManagerProperties();
  EXPECT_FALSE(network_state_handler_->GetNetworkState(wifi_path));

  // Add the service (simulating a network coming back in range) and verify that
  // the NetworkState was created with the same GUID.
  AddService(wifi_path, wifi_path, shill::kTypeWifi, shill::kStateOnline);
  UpdateManagerProperties();
  network = network_state_handler_->GetNetworkState(wifi_path);
  ASSERT_TRUE(network);
  EXPECT_EQ(wifi_guid, network->guid());

  // Also verify FavoriteState (mostly to test the stub behavior).
  favorite = network_state_handler_->GetFavoriteStateFromServicePath(
      wifi_path, is_service_configured);
  ASSERT_TRUE(favorite);
  EXPECT_EQ(wifi_guid, favorite->guid());
}

TEST_F(NetworkStateHandlerTest, NetworkGuidNotInProfile) {
  const std::string wifi_path = "wifi_with_guid";
  const bool is_service_configured = false;

  // Add a network without adding it to a profile.
  AddService(wifi_path, wifi_path, shill::kTypeWifi, shill::kStateOnline);
  UpdateManagerProperties();

  // Verify that a FavoriteState exists with an assigned GUID.
  const FavoriteState* favorite =
      network_state_handler_->GetFavoriteStateFromServicePath(
          wifi_path, is_service_configured);
  ASSERT_TRUE(favorite);
  std::string wifi_guid = favorite->guid();
  EXPECT_FALSE(wifi_guid.empty());

  // Verify that a NetworkState exists with the same GUID.
  const NetworkState* network =
      network_state_handler_->GetNetworkState(wifi_path);
  ASSERT_TRUE(network);
  EXPECT_EQ(wifi_guid, network->guid());

  // Remove the service (simulating a network going out of range).
  service_test_->RemoveService(wifi_path);
  UpdateManagerProperties();
  EXPECT_FALSE(network_state_handler_->GetNetworkState(wifi_path));

  // Add the service (simulating a network coming back in range) and verify that
  // the NetworkState was created with the same GUID.
  AddService(wifi_path, wifi_path, shill::kTypeWifi, shill::kStateOnline);
  UpdateManagerProperties();
  network = network_state_handler_->GetNetworkState(wifi_path);
  ASSERT_TRUE(network);
  EXPECT_EQ(wifi_guid, network->guid());

  // Also verify FavoriteState (mostly to test the stub behavior).
  favorite = network_state_handler_->GetFavoriteStateFromServicePath(
      wifi_path, is_service_configured);
  ASSERT_TRUE(favorite);
  EXPECT_EQ(wifi_guid, favorite->guid());
}

}  // namespace chromeos
