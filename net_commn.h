#pragma once

#include "net_commn.h"
#include <algorithm>
#include <asio/connect.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <system_error>
#include <thread>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#endif

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define ASIO_STANDALONE
#include <asio.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>

template <typename T> struct message_header {
  // maybe char id[10]
  T id;
  uint32_t size = 0;
};
template <typename T> struct message {
  message_header<T> header{};
  std::vector<uint8_t> body;

  size_t size() const { return body.size(); }

  friend std::ostream &operator<<(std::ostream &os, const message<T> &msg) {
    os << "ID:" << int(msg.header.id) << " Size:" << msg.header.size;
    return os;
  }
  template <typename DataType>
  friend message<T> &operator<<(message<T> &msg, const DataType &data) {
    static_assert(std::is_standard_layout<DataType>::value,
                  "Data is too complex to be pushed into vector");

    size_t i = msg.body.size();
    msg.body.resize(i + sizeof(DataType));
    std::memcpy(msg.body().data() + i, &data, sizeof(DataType));
    msg.header.size = msg.size();
    return msg;
  }

  template <typename DataType>
  friend message<T> &operator>>(message<T> &msg, DataType &data) {
    static_assert(std::is_standard_layout<DataType>::value,
                  "Data is too complex to be pushed into vector");
    size_t i = msg.body.size() - sizeof(DataType);

    std::memcpy(&data, msg.body.size() + i, sizeof(DataType));
    msg.body.resize(i);
    msg.header.size = msg.body.size();
    return msg;
  }
};

template <typename T> class connection;
template <typename T> struct owned_message {
  std::shared_ptr<connection<T>> remote{nullptr};
  message<T> msg;
  friend std::ostream &operator<<(std::ostream &os,
                                  const owned_message<T> &msg) {
    os << msg.msg;
    return os;
  }
};
//人任何时候，都不要放弃对艺术的追求。

template <typename T> class tsqueue {
public:
  tsqueue() = default;
  tsqueue(const tsqueue<T> &) = delete;
  virtual ~tsqueue();

protected:
  std::mutex muxQueue;
  std::deque<T> deqQueue;
  std::condition_variable cvBlocking;
  std::mutex muxBlocking;

public:
  const T &font() {
    std::scoped_lock lock(muxQueue);
    return deqQueue.font();
  }
  const T &back() {
    std::scoped_lock lock(muxQueue);
    return deqQueue.back();
  }
  T pop_front() {
    std::scoped_lock lock(muxQueue);
    auto t = std::move(deqQueue.front());
    deqQueue.pop_front();
    return t;
  }
  T pop_back() {
    std::scoped_lock locl(muxQueue);
    auto t = std::move(deqQueue.back());
    deqQueue.pop_back();
    return t;
  }
  void push_back(const T &item) {
    std::scoped_lock lock(muxQueue);
    deqQueue.emplace_back(std::move(item));
    std::unique_lock<std::mutex> ul(muxBlocking);
    cvBlocking.notify_one();
  }
  void push_front(const T &item) {
    std::scoped_lock lock(muxQueue);
    deqQueue.emplace_front(item);
    std::unique_lock<std::mutex> ul(muxBlocking);
    cvBlocking.notify_one();
  }
  bool empty() {
    std::scoped_lock lock(muxQueue);
    return deqQueue.empty();
  }
  size_t count() {
    std::scoped_lock lock(muxQueue);
    return deqQueue.size();
  }

  void clear() {
    std::scoped_lock lock(muxQueue);
    deqQueue.clear();
  }

  void wait() {
    while (empty()) {
      std::unique_lock<std::mutex> ul(muxBlocking);
      cvBlocking.wait(ul);
    }
  }
};

template <typename T> class server_interface;
template <typename T>
class connection : public std::enable_shared_from_this<connection<T>> {

public:
  enum class owner { server, client };

protected:
  asio::ip::tcp::socket m_socket;
  asio::io_context &m_asioContext;
  tsqueue<message<T>> m_qMessageOut;
  tsqueue<owned_message<T>> m_qMessageIn;
  message<T> m_msgTemporaryIn;
  owner m_nOwnerType = owner::server;

  uint64_t m_nHandShakeOut{0};
  uint64_t m_nHandShakeIn{0};
  uint64_t m_nHandShakeCheck{0};

  bool m_bValidHandshake{false};
  bool m_bConnectionEstablished{false};

  uint32_t id{0};

public:
  connection(owner parent, asio::io_context &asioContext,
             asio::ip::tcp::socket socket, tsqueue<owned_message<T>> &qIn)
      : m_asioContext(asioContext), m_socket(std::move(socket)),
        m_qMessageIn(qIn) {
    m_nOwnerType = parent;
    if (m_nOwnerType == owner::server) {
      m_nHandShakeOut =
          uint64_t(std::chrono::system_clock::now().time_since_epoch().count());

    } else {
    }
  }

  virtual ~connection() {}

  uint32_t GetID() const { return id; }

public:
  void ConnectToclient(server_interface<T> *server,

                       uint32_t uid = 0) {
    if (m_nOwnerType == owner::server) {
      if (m_socket.is_open()) {
        id = uid;
        WriteValidation();
      }
    }
  }
  void ConnectToServer(const asio::ip::tcp::resolver::results_type &endpoints) {
    if(m_nOwnerType == owner::client) {
      asio::async_connect(m_socket , endpoints, [this](std::error_code ec, asio::ip::tcp::endpoint endpoint){
        if(!ec) {
          ReadValidation();
        }
      });
    }
  }
  void Disconnect() {
    if(IsConnected()) {
      asio::post(m_asioContext,[this](){m_socket.close();});
    }
  }
  bool IsConnected() const {return m_socket.is_open();}
  void StartListening() {}

  void WriteValidation() {
    asio::async_write(m_socket,
                      asio::buffer(&m_nHandShakeOut, sizeof(uint64_t)),

                      [this](std::error_code ec, std::size_t length) {
                        if (!ec) {
                          if (m_nOwnerType == owner::client) {
                          }
                        }
                      });
  }
  void ReadValidation(server_interface<T> *server = nullptr) {
    asio::read(m_socket, asio::buffer(&m_nHandShakeIn, sizeof(uint64_t)),
               [this, server](std::error_code ec, std::size_t length) {
                 if (!ec) {
                   if (m_nOwnerType == owner::server) {
                     if (m_nHandShakeIn == m_nHandShakeCheck) {
                       std::cout << "Client Validated " << std::endl;
                       // TODO
                       ReadHeader();
                     } else {
                       std::cout << "Client Disconnected(Fail Validation)"
                                 << std::endl;
                       m_socket.close();
                     }
                   } else {
                     std::cout << "Clinet Disconnected (ReadValidation)"
                               << std::endl;
                     m_socket.close();
                   }
                 }
               });
  }

  void ReadHeader() {
    asio::async_read(
        m_socket,
        asio::buffer(&m_msgTemporaryIn.header, sizeof(message_header<T>)),
        [this](std::error_code ec, std::size_t length) {
          if (!ec) {
            if (m_msgTemporaryIn.header.size > 0) {
              m_msgTemporaryIn.body.resize(m_msgTemporaryIn.header.size);
            }
          }
        }

    );
  }
  void ReadBody() {
    asio::async_read(m_socket,
                     asio::buffer(m_msgTemporaryIn.body.data(),
                                  m_msgTemporaryIn.body.size()),
                     [this](std::error_code ec, std::size_t length) {
                       if (!ec) {
                       }
                     }

    );
  }
  void AddToIncomingMessageQueue() {
    if (m_nOwnerType == owner::server) {
      m_qMessageIn.push_back({this->shared_from_this(), m_msgTemporaryIn});
    } else {
      m_qMessageIn.push_back({nullptr, m_msgTemporaryIn});
    }
  }
};