// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/network_handler.h"

#include "base/threading/worker_pool.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/network/client_cert_resolver.h"
#include "chromeos/network/geolocation_handler.h"
#include "chromeos/network/managed_network_configuration_handler_impl.h"
#include "chromeos/network/network_activation_handler.h"
#include "chromeos/network/network_cert_migrator.h"
#include "chromeos/network/network_configuration_handler.h"
#include "chromeos/network/network_connection_handler.h"
#include "chromeos/network/network_device_handler_impl.h"
#include "chromeos/network/network_event_log.h"
#include "chromeos/network/network_profile_handler.h"
#include "chromeos/network/network_profile_observer.h"
#include "chromeos/network/network_sms_handler.h"
#include "chromeos/network/network_state_handler.h"
#include "chromeos/network/network_state_handler_observer.h"

namespace chromeos {

static NetworkHandler* g_network_handler = NULL;

NetworkHandler::NetworkHandler()
    : message_loop_(base::MessageLoopProxy::current()) {
  CHECK(DBusThreadManager::IsInitialized());

  network_event_log::Initialize();

  network_state_handler_.reset(new NetworkStateHandler());
  network_device_handler_.reset(new NetworkDeviceHandlerImpl());
  network_profile_handler_.reset(new NetworkProfileHandler());
  network_configuration_handler_.reset(new NetworkConfigurationHandler());
  managed_network_configuration_handler_.reset(
      new ManagedNetworkConfigurationHandlerImpl());
  if (CertLoader::IsInitialized()) {
    network_cert_migrator_.reset(new NetworkCertMigrator());
    client_cert_resolver_.reset(new ClientCertResolver());
  }
  network_activation_handler_.reset(new NetworkActivationHandler());
  network_connection_handler_.reset(new NetworkConnectionHandler());
  network_sms_handler_.reset(new NetworkSmsHandler());
  geolocation_handler_.reset(new GeolocationHandler());
}

NetworkHandler::~NetworkHandler() {
  network_event_log::Shutdown();
}

void NetworkHandler::Init() {
  network_state_handler_->InitShillPropertyHandler();
  network_device_handler_->Init(network_state_handler_.get());
  network_profile_handler_->Init(network_state_handler_.get());
  network_configuration_handler_->Init(network_state_handler_.get());
  managed_network_configuration_handler_->Init(
      network_state_handler_.get(),
      network_profile_handler_.get(),
      network_configuration_handler_.get());
  network_connection_handler_->Init(
      network_state_handler_.get(),
      network_configuration_handler_.get(),
      managed_network_configuration_handler_.get());
  if (network_cert_migrator_)
    network_cert_migrator_->Init(network_state_handler_.get());
  if (client_cert_resolver_) {
    client_cert_resolver_->Init(network_state_handler_.get(),
                                managed_network_configuration_handler_.get());
  }
  network_sms_handler_->Init();
  geolocation_handler_->Init();
}

// static
void NetworkHandler::Initialize() {
  CHECK(!g_network_handler);
  g_network_handler = new NetworkHandler();
  g_network_handler->Init();
}

// static
void NetworkHandler::Shutdown() {
  CHECK(g_network_handler);
  delete g_network_handler;
  g_network_handler = NULL;
}

// static
NetworkHandler* NetworkHandler::Get() {
  CHECK(g_network_handler)
      << "NetworkHandler::Get() called before Initialize()";
  return g_network_handler;
}

// static
bool NetworkHandler::IsInitialized() {
  return g_network_handler;
}

NetworkStateHandler* NetworkHandler::network_state_handler() {
  return network_state_handler_.get();
}

NetworkDeviceHandler* NetworkHandler::network_device_handler() {
  return network_device_handler_.get();
}

NetworkProfileHandler* NetworkHandler::network_profile_handler() {
  return network_profile_handler_.get();
}

NetworkConfigurationHandler* NetworkHandler::network_configuration_handler() {
  return network_configuration_handler_.get();
}

ManagedNetworkConfigurationHandler*
NetworkHandler::managed_network_configuration_handler() {
  return managed_network_configuration_handler_.get();
}

NetworkActivationHandler* NetworkHandler::network_activation_handler() {
  return network_activation_handler_.get();
}

NetworkConnectionHandler* NetworkHandler::network_connection_handler() {
  return network_connection_handler_.get();
}

NetworkSmsHandler* NetworkHandler::network_sms_handler() {
  return network_sms_handler_.get();
}

GeolocationHandler* NetworkHandler::geolocation_handler() {
  return geolocation_handler_.get();
}

}  // namespace chromeos
