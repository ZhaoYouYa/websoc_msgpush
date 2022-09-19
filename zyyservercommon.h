#pragma once

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <thread>
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
#include <tuple>

namespace Silenced {

static void PostToMainThread(std::function<void()> f);
class Server;
class Connection;
static Server *server{nullptr};
static void ClearThisConnection(std::shared_ptr<Connection>);
struct MessageHeader {
  uint32_t _id;
  uint32_t _size{0};
};

struct Message {
  MessageHeader _header{};
  std::vector<uint8_t> _body{};
  template <typename DataType>
  friend auto &operator<<(Message &msg, const DataType &data) {

    static_assert(std::is_standard_layout<DataType>::value,
                  "Data is too complex to be pushed into vector");
    size_t i = msg._body.size();
    msg._body.resize(i + sizeof(DataType));
    std::memcpy(msg._body.data() + i, &data, sizeof(DataType));
    msg._header._size = msg._body.size();
    return msg;
  }
  template <typename DataType>
  friend auto &operator>>(Message &msg, const DataType &data) {
    static_assert(std::is_standard_layout<DataType>::value,
                  "Data is too complex to be pushed into vector");
    size_t i = msg._body.size() - sizeof(DataType);
    std::memcpy(&data, msg._body.size() + i, sizeof(DataType));
    msg._body.resize(i);
    msg._header._size = msg._body.size();
    return msg;
  }
};

template <typename T, bool isCanWait> class Queue {
protected:
  std::mutex _muxQueue;
  std::deque<T> _deqQueue;
  std::condition_variable _cvBlocking;
  std::mutex _muxBlocking;

public:
  Queue() = default;
  Queue(const Queue<T, isCanWait> &) = delete;
  virtual ~Queue() {}

public:
  const T &front() {
    std::scoped_lock lock(_muxQueue);
    return _deqQueue.front();
  }
  T pop_front() {
    std::scoped_lock lock(_muxQueue);
    auto t = std::move(_deqQueue.front());
    _deqQueue.pop_front();
    return t;
  }
  bool empty() {
    std::scoped_lock lock(_muxQueue);
    return _deqQueue.empty();
  }
  size_t size() {
    std::scoped_lock loc(_muxQueue);
    return _deqQueue.size();
  }
  size_t count() { return size(); }
  void clear() {
    std::scoped_lock lock(_muxQueue);
    _deqQueue.clear();
  }
  void wait() {
    if constexpr (isCanWait) {
      while (empty()) {
        std::unique_lock<std::mutex> ul(_muxBlocking);
        _cvBlocking.wait(ul);
      }
    }
  }
  void push_back(const T &it) {

    std::scoped_lock lock(_muxQueue);
    _deqQueue.emplace_back(std::move(it));

    if constexpr (isCanWait) {
      std::unique_lock<std::mutex> ul(_muxBlocking);
      _cvBlocking.notify_one();
    }
  }
};

class Connection;
struct OwnedMessage {
  std::shared_ptr<Connection> remote{nullptr};
  Message msg;
};
class Connection : public std::enable_shared_from_this<Connection> {
protected:
  asio::ip::tcp::socket _socket;
  asio::io_context &_asioContext;
  Queue<Message, true> _messageOut;
  Queue<OwnedMessage, true> &_messageIn;
  Message _msgTemporaryIn;
  char _pwd[6];
  uint32_t _id{0};
  std::function<void()> _clearThisConnection;

public:
  Connection(asio::io_context &asioContext, asio::ip::tcp::socket socket,
             Queue<OwnedMessage, true> &in)
      : _socket(std::move(socket)), _asioContext(asioContext), _messageIn(in) {}

  virtual ~Connection() {}
  uint32_t getId() { return _id; }

public:
  void startClient(uint32_t uid = 0) {
    if (_socket.is_open()) {
      _id = uid;
      readValidation();
    }
  }
  bool isConnect() const { return _socket.is_open(); }
  void disconnect() {
    if (isConnect()) {
      asio::post(_asioContext, [this]() { _socket.close(); });
    }
  }

private:
  void readValidation() {
    asio::async_read(_socket, asio::buffer(_pwd, sizeof(_pwd)),
                     [this](std::error_code ec, std::size_t length) {
                       if (!ec) {
                         std::string_view pwd(_pwd);
                         std::cout << "Pwd: " << pwd << std::endl;
                         if (pwd == "zyy123") {

                           readMessage();
                         } else {
                           std::cout << "pwd failed!\n";
                           // Validation Failed!

                           _socket.close();
                           PostToMainThread([this]() {
                             ClearThisConnection(this->shared_from_this());
                           });
                         }

                       } else {
                         // Err occur
                         _socket.close();
                         PostToMainThread([this]() {
                           ClearThisConnection(this->shared_from_this());
                         });
                       }
                     });
  }
  void readMessage() {
    asio::async_read(
        _socket, asio::buffer(&_msgTemporaryIn, sizeof(MessageHeader)),
        [this](std::error_code ec, std::size_t length) {
          if (!ec) {
            if (_msgTemporaryIn._header._size > 0) {
              std::cout << "Header Size: " << _msgTemporaryIn._header._size
                        << std::endl;
              std::cout << "Header Id: " << _msgTemporaryIn._header._id
                        << std::endl;
              _msgTemporaryIn._body.resize(_msgTemporaryIn._header._size);
              asio::async_read(_socket,
                               asio::buffer(_msgTemporaryIn._body.data(),
                                            _msgTemporaryIn._header._size),
                               [this](std::error_code ec, std::size_t length) {
                                 if (!ec) {
                                   _messageOut.push_back(_msgTemporaryIn);
                                   std::cout << "Body: " << std::endl;
                                   for (auto &x : _msgTemporaryIn._body) {
                                     std::cout << x;
                                   }
                                   std::cout << std::endl;
                                   readMessage();
                                 } else {
                                   // Some Err Occur
                                   _socket.close();
                                   PostToMainThread([this]() {
                                     ClearThisConnection(
                                         this->shared_from_this());
                                   });
                                 }
                               });
            } else {
              readMessage();
            }
          } else {
            //
            std::cout << ec.message();
            _socket.close();
            PostToMainThread(
                [this]() { ClearThisConnection(this->shared_from_this()); });
          }
        });
  }
  void writeMessage() {

    _messageOut.wait();
    asio::async_write(
        _socket,
        asio::buffer(&_messageOut.front()._header, sizeof(MessageHeader)),
        [this](std::error_code ec, std::size_t lenght) {
          if (!ec) {
            if (_messageOut.front()._body.size() > 0) {

              asio::async_write(_socket,
                                asio::buffer(_messageOut.front()._body.data(),
                                             _messageOut.front()._body.size()),
                                [this](std::error_code ec, std::size_t length) {
                                  if (!ec) {
                                    _messageOut.pop_front();
                                    writeMessage();
                                  } else {
                                    std::cout
                                        << "Body ec Message:" << ec.message()
                                        << std::endl;
                                  }
                                });
            } else {
              _messageOut.pop_front();
              writeMessage();
            }
          } else {
            // write Err
            std::cout << "Head ec Message:" << ec.message() << std::endl;
            _socket.close();
          }
        });
  }
};

class Server {
protected:
  Queue<OwnedMessage, true> _messageIn;
  std::vector<std::shared_ptr<Connection>> _connections;
  asio::io_context _asioContext;
  std::thread _threadContext;
  asio::ip::tcp::acceptor _asioAcceptor;

  uint32_t _IDCounter{10000};

public:
  std::deque<std::function<void()>> _task;
  std::condition_variable _cvBlocking;
  std::mutex _muxBlocking;
  std::mutex _muxQueue;

public:
  void postTaskToMainThread(std::function<void()> f) {
    std::scoped_lock lock(_muxQueue);
    bool isEmpty = _task.empty();
    _task.emplace_back(f);
    if (isEmpty) {
      std::unique_lock<std::mutex> ul(_muxBlocking);
      _cvBlocking.notify_one();
    }
  }

public:
  Server(uint16_t port)
      : _asioAcceptor(_asioContext,
                      asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {
    assert(server == nullptr);
    server = this;
  }
  virtual ~Server() {}
  int start() {
    try {
      waitForClientConnection();
      _threadContext = std::thread([this]() { _asioContext.run(); });
      while (true) {
        if (_task.empty()) {
          std::unique_lock<std::mutex> ul(_muxBlocking);
          _cvBlocking.wait(ul);
        } else {
          std::scoped_lock lock(_muxQueue);
          auto f = _task.front();
          f();
          _task.pop_front();
        }
      }
      return -1;
    } catch (std::exception &e) {
      std::cerr << "[SERVER] Exception: " << e.what() << "\n";
      return -1;
    }
  }
  void waitForClientConnection() {
    _asioAcceptor.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
          if (!ec) {

            std::cout << "Remote Connect Info:" << socket.remote_endpoint()
                      << std::endl;

            _connections.push_back(std::make_shared<Connection>(
                _asioContext, std::move(socket), _messageIn));
            _connections.back()->startClient(_IDCounter++);
          } else {
            // TODO LOG message
          }
          waitForClientConnection();
        });
  }

  void clearConnection(std::shared_ptr<Connection> it) {

    auto isIt = [&it](std::shared_ptr<Connection> &i) {
      if (i.get() == it.get()) {
        return true;
      }
      return false;
    };
    auto result = std::find_if(begin(_connections), end(_connections), isIt);

    // at runtime C++ don't do any decide!
    if (result != _connections.end()) {
      _connections.erase(result);
    }
  }
};

static void PostToMainThread(std::function<void()> f) {
  server->postTaskToMainThread(f);
}
static void ClearThisConnection(std::shared_ptr<Connection> it) {
  server->clearConnection(it);
}
} // namespace Silenced
