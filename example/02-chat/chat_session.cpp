/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "chat_session.hpp"

#include <future>
#include <iostream>

#include <boost/assert.hpp>

/// errors, which are placed into outcome::result, must be described as follows
OUTCOME_CPP_DEFINE_CATEGORY(custom_protocol, ChatSession::Error, e) {
  using E = custom_protocol::ChatSession::Error;
  switch (e) {
    case E::STREAM_CLOSED_FOR_READS:
      return "stream is closed for reads";
    case E::STREAM_CLOSED_FOR_WRITES:
      return "stream is closed for writes";
  }
  return "unknown error";
}

namespace custom_protocol {
  ChatSession::ChatSession(std::shared_ptr<libp2p::connection::Stream> stream,
                           ChatConfig config)
      : stream_{std::move(stream)},
        config_{config},
        read_buffer_(config_.max_message_size, 0) {
    BOOST_ASSERT_MSG(stream_, "passed stream is null");
    BOOST_ASSERT_MSG(config_.max_message_size > 0,
                     "maximum size of the message cannot be zero");
  }

  void ChatSession::start() {
    BOOST_ASSERT_MSG(!active_, "double starting ChatSession");
    active_ = true;

    std::cout << "New chat session is started!\n";

    readNextMessage();
    writeNextMessage();
  }

  void ChatSession::stop() {
    BOOST_ASSERT_MSG(active_, "trying to stop an inactive ChatSession");
    active_ = false;
    stream_->reset();
  }

  void ChatSession::readNextMessage() {
    if (!active_) {
      return;
    }
    if (stream_->isClosedForRead()) {
      return config_.bus.getChannel<event::MessageReadErrorChannel>().publish(
          Error::STREAM_CLOSED_FOR_READS);
    }

    // while reading or writing it is essential to capture *this* using a
    // shared_ptr, as such approach allows it not to be destroyed
    stream_->readSome(
        read_buffer_, config_.max_message_size,
        [self{shared_from_this()}](outcome::result<size_t> read_res) mutable {
          if (!read_res) {
            // some error happened during the read - reset an underlying stream,
            // as most probably we can't use it anymore, - and report the error
            self->stream_->reset();
            return self->config_.bus
                .getChannel<event::MessageReadErrorChannel>()
                .publish(read_res.error());
          }

          // write a received message into the stream and read a next one
          std::string msg{self->read_buffer_.data(),
                          self->read_buffer_.data() + read_res.value()};
          std::cout << ">> " << msg << "\n";
          self->readNextMessage();
        });
  }

  void ChatSession::writeNextMessage() {
    while (true) {
      if (!active_) {
        return;
      }
      if (stream_->isClosedForWrite()) {
        return config_.bus.getChannel<event::MessageWriteErrorChannel>()
            .publish(Error::STREAM_CLOSED_FOR_WRITES);
      }

      // asynchronously wait for the user's message for the other side
      std::future<std::string> input_msg_fut = std::async([] {
        std::string input_msg;
        std::cin >> input_msg;
        return input_msg;
      });
      input_msg_fut.wait();
      auto msg = input_msg_fut.get();

      ByteArray msg_bytes{msg.data(), msg.data() + msg.size()};
      write_queue_.push({std::move(msg), std::move(msg_bytes)});
      if (!writing_) {
        doWrite();
      }
    }
  }

  void ChatSession::doWrite() {
    if (write_queue_.empty()) {
      return;
    }
    if (stream_->isClosedForWrite()) {
      return config_.bus.getChannel<event::MessageWriteErrorChannel>().publish(
          Error::STREAM_CLOSED_FOR_WRITES);
    }

    const auto &next_msg_bytes = write_queue_.front().second;
    stream_->write(
        next_msg_bytes, next_msg_bytes.size(),
        [self{shared_from_this()}](outcome::result<size_t> write_res) {
          if (!write_res) {
            self->stream_->reset();
            return self->config_.bus
                .getChannel<event::MessageWriteErrorChannel>()
                .publish(write_res.error());
          }

          // write message to the stream only when it's received by the
          // destination
          const auto &msg = self->write_queue_.front().first;
          std::cout << "<< " << msg << "\n";
          self->write_queue_.pop();

          // write next message from the queue
          self->doWrite();
        });
  }

}  // namespace custom_protocol
