/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <string>

#include <boost/program_options.hpp>
#include <libp2p/common/literals.hpp>
#include <libp2p/event/bus.hpp>
#include <libp2p/host/basic_host.hpp>
#include <libp2p/injector/host_injector.hpp>
#include "chat_protocol.hpp"

/**
 * Parse address from the command line
 * @return parsed address and bool, which shows, if the node was launched as a
 * server
 */
std::pair<std::string, bool> parseAddressFromConsole(int argc, char *argv[]) {
  namespace po = boost::program_options;
  static const std::string kListenAddressOption = "-l";
  static const std::string kClientAddressOption = "-c";

  po::options_description desc{};
  // clang-format off
  desc.add_options()
    (kListenAddressOption.data(), po::value<std::string>(), "listen address")
    (kClientAddressOption.data(), po::value<std::string>(), "client address")
  ;
  // clang-format on

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  auto is_server = vm.count(kListenAddressOption);
  auto is_client = vm.count(kClientAddressOption);
  if (!is_server && !is_client) {
    std::cerr << "listen or client address must be specified with either "
              << kListenAddressOption << " or " << kClientAddressOption
              << " option\n";
    std::exit(EXIT_FAILURE);
  }
  if (is_server && is_client) {
    std::cerr << "node must be launched as either server or client\n";
    std::exit(EXIT_FAILURE);
  }

  if (is_server) {
    return {vm[kListenAddressOption].as<std::string>(), true};
  }
  return {vm[kClientAddressOption].as<std::string>(), false};
}

void chatSessionCloseHandler(std::error_code ec) {
  std::cout << "Chat session was closed: " << ec.message() << "\n";
  std::exit(EXIT_SUCCESS);
}

void launchServer(std::shared_ptr<libp2p::Host> host,
                  std::string_view listen_address,
                  custom_protocol::ChatProtocol &chat) {
  auto ma = libp2p::multi::Multiaddress::create(listen_address);
  if (!ma) {
    std::cerr << "cannot create listen address: " << ma.error().message();
    std::exit(EXIT_FAILURE);
  }

  auto listen_res = host->listen(ma.value());
  if (!listen_res) {
    std::cerr << "host cannot listen the given multiaddress: "
              << listen_res.error().message() << "\n";
    std::exit(EXIT_FAILURE);
  }

  // set a handler for Chat protocol
  host->setProtocolHandler(chat.getProtocolId(), [&chat](auto &&stream_ptr) {
    chat.handle(std::forward<decltype(stream_ptr)>(stream_ptr));
  });

  // finally, start the Host
  host->start();
  std::cout << "Server started\nListening on: " << ma.value().getStringAddress()
            << "\nPeer id: " << host->getPeerInfo().id.toBase58() << "\n";
}

void launchClient(std::shared_ptr<libp2p::Host> host,
                  std::string_view client_address,
                  custom_protocol::ChatProtocol &chat) {
  auto ma = libp2p::multi::Multiaddress::create(client_address);
  if (!ma) {
    std::cerr << "cannot create client address: " << ma.error().message()
              << "\n";
    std::exit(EXIT_FAILURE);
  }
  auto client_ma = std::move(ma.value());

  auto peer_id_str = client_ma.getPeerId();
  if (!peer_id_str) {
    std::cerr << "no PeerId in the provided client address\n";
    std::exit(EXIT_FAILURE);
  }

  auto peer_id_res = libp2p::peer::PeerId::fromBase58(*peer_id_str);
  if (!peer_id_res) {
    std::cerr << "invalid PeerId in the provided client address: "
              << peer_id_res.error().message() << "\n";
    std::exit(EXIT_FAILURE);
  }
  auto peer_id = std::move(peer_id_res.value());

  // finally, open a stream over Chat protocol
  host->newStream(libp2p::peer::PeerInfo{peer_id, {client_ma}},
                  chat.getProtocolId(), [&chat](auto &&stream_res) {
                    if (!stream_res) {
                      std::cerr << "cannot open a stream to the server: "
                                << stream_res.error().message() << "\n";
                      std::exit(EXIT_FAILURE);
                    }
                    chat.handle(std::forward<decltype(stream_res)>(stream_res));
                  });
}

int main(int argc, char *argv[]) {
  using libp2p::crypto::Key;
  using libp2p::crypto::KeyPair;
  using libp2p::crypto::PrivateKey;
  using libp2p::crypto::PublicKey;
  using libp2p::common::operator""_unhex;

  // this keypair generates a PeerId
  // "12D3KooWLs7RC93EGXZzn9YdKyZYYx3f9UjTLYNX1reThpCkFb83"
  const KeyPair server_keypair{
      PublicKey{
          {Key::Type::Ed25519,
           "a4249ea6d62bdd8bccf62257ac4899ff2847963228b388fda288db5d64e517e0"_unhex}},
      PrivateKey{
          {Key::Type::Ed25519,
           "4a9361c525840f7086b893d584ebbe475b4ec7069951d2e897e8bceb0a3f35ce"_unhex}}};

  const KeyPair client_keypair{
      PublicKey{
          {Key::Type::Ed25519,
           "f39c6aefc5fc16cd24eaccebe4b79b6a4d40914c08797b9c72a34f06aa8ff8d6"_unhex}},
      PrivateKey{
          {Key::Type::Ed25519,
           "be491f544835c2146aecd0e6e435142ffac5e7dc5a0b4f3f88f9a7ce503cb446"_unhex}}};

  auto address = parseAddressFromConsole(argc, argv);
  const auto &our_keypair = address.second ? server_keypair : client_keypair;

  // create a default Host via an injector, overriding a random-generated
  // keypair with ours
  auto injector = libp2p::injector::makeHostInjector(
      libp2p::injector::useKeyPair(our_keypair));
  auto host = injector.create<std::shared_ptr<libp2p::Host>>();

  // bus, over which events are delivered; we will use it to catch such events
  // as, for example, close of the chat session
  libp2p::event::Bus bus;
  bus.getChannel<custom_protocol::event::MessageWriteErrorChannel>().subscribe(
      chatSessionCloseHandler);
  bus.getChannel<custom_protocol::event::MessageReadErrorChannel>().subscribe(
      chatSessionCloseHandler);

  // create an implementation of the user protocol - Chat
  custom_protocol::ChatProtocol chat{{bus}};

  // launch the Host
  auto context = injector.create<std::shared_ptr<boost::asio::io_context>>();
  context->post([host{std::move(host)}, &address, &chat] {
    if (address.second) {
      launchServer(std::move(host), address.first, chat);
    } else {
      launchClient(std::move(host), address.first, chat);
    }
  });

  // run the IO context
  context->run();
}
