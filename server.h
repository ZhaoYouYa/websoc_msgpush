#pragma once
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <drogon/HttpAppFramework.h>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>


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
#include <drogon/HttpTypes.h>
#include <drogon/WebSocketConnection.h>
#include <drogon/WebSocketController.h>
#include <drogon/utils/FunctionTraits.h>
#include <filesystem>
#include <glog/logging.h>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
using namespace drogon;

namespace Silenced {

static void PostToMainThread(std::function<void()> f);
class Server;
class Connection;
static Server *server{nullptr};
static void ClearThisConnection(std::shared_ptr<Connection> c);
static void NotifyMainThread();
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
class Connection;
struct OwnedMessage {
  std::shared_ptr<Connection> remote{nullptr};
  Message msg;
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
      _cvBlocking.notify_one();
    }
  }
};

class Connection : public std::enable_shared_from_this<Connection> {
public:
  asio::ip::tcp::socket _socket;
  asio::io_context &_asioContext;
  Queue<Message *, false> _messageOut;
  std::unique_ptr<Message> _msgTemporaryOut{nullptr};
  Queue<OwnedMessage, false> &_messageIn;
  Message _msgTemporaryIn;
  char _pwd[6];
  uint32_t _id{0};
  std::function<void()> _clearThisConnection;

public:
  Connection(asio::io_context &asioContext, asio::ip::tcp::socket socket,
             Queue<OwnedMessage, false> &in)
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
                         if (pwd == "zyy123") {
                           readMessage();
                         } else {
                           LOG(WARNING) << "pwd err";
                           _socket.close();
                           PostToMainThread([this]() {
                             ClearThisConnection(this->shared_from_this());
                           });
                         }
                       } else {
                         LOG(WARNING) << ec.message();
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
              _msgTemporaryIn._body.resize(_msgTemporaryIn._header._size);
              asio::async_read(_socket,
                               asio::buffer(_msgTemporaryIn._body.data(),
                                            _msgTemporaryIn._header._size),
                               [this](std::error_code ec, std::size_t length) {
                                 if (!ec) {
                                   OwnedMessage msg;
                                   msg.msg = _msgTemporaryIn;
                                   msg.remote = this->shared_from_this();
                                   _messageIn.push_back(msg);
                                   NotifyMainThread();
                                   readMessage();
                                 } else {
                                   LOG(WARNING) << ec.message();
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
            LOG(WARNING) << ec.message();
            _socket.close();
            PostToMainThread(
                [this]() { ClearThisConnection(this->shared_from_this()); });
          }
        });
  }

public:
  // Thread Safe
  void writeMessage() {
    if (_msgTemporaryOut.get() != nullptr || _messageOut.empty()) {
      return;
    }

    _msgTemporaryOut.reset(_messageOut.pop_front());
    asio::async_write(
        _socket,
        asio::buffer(&_msgTemporaryOut->_header, sizeof(MessageHeader)),
        [this](std::error_code ec, std::size_t lenght) {
          if (!ec) {
            if (_msgTemporaryOut->_body.size() > 0) {

              asio::async_write(_socket,
                                asio::buffer(_msgTemporaryOut->_body.data(),
                                             _msgTemporaryOut->_body.size()),
                                [this](std::error_code ec, std::size_t length) {
                                  if (!ec) {
                                    _msgTemporaryOut.reset(nullptr);
                                    writeMessage();
                                  } else {
                                    LOG(WARNING) << ec.message();
                                  }
                                });
            } else {
              _msgTemporaryOut.reset(nullptr);
              writeMessage();
            }
          } else {
            // write Err
            LOG(WARNING) << ec.message();
            _socket.close();
          }
        });
  }
};
class WebSock : public WebSocketController<WebSock, false> {
public:
  WebSock(){};

public:
  virtual void handleNewMessage(const WebSocketConnectionPtr &WebPr,
                                std::string &&Str,
                                const WebSocketMessageType &) override{
      // WebPr->send(Str);
      // DO NOTHING
  };
  virtual void
  handleNewConnection(const HttpRequestPtr &Request,
                      const WebSocketConnectionPtr &WebSocPtr) override {
    auto id = Request->parameters().find("id");
    if (id != Request->parameters().end()) {
      std::scoped_lock lock(_m);
      _connection[id->second] = WebSocPtr;
      LOG(INFO) << id->second << " Connected!";
    }
  };
  virtual void
  handleConnectionClosed(const WebSocketConnectionPtr &WebSocPtr) override {
    std::scoped_lock lock(_m);
    for (auto &it : _connection) {
      if (it.second == WebSocPtr) {
        LOG(INFO) << it.first << " Disconnected!";
        _connection.erase(it.first);

        break;
      }
    }
  };
  void sendMessageToWebSocket(const OwnedMessage &msg) {
    try {
      std::scoped_lock lock(_m);
      std::string info((char *)msg.msg._body.data());
      std::regex regex_(R"(\$.*?\$)");
      std::smatch match_;
      std::string::const_iterator iter_begin = info.cbegin();
      std::string::const_iterator iter_end = info.cend();

      if (regex_search(iter_begin, iter_end, match_, regex_)) {
        std::string aim(match_[0]);
        info.replace(0, aim.length(), "");
        aim = aim.substr(1, aim.length() - 2);
        auto backchar = aim.back();
        auto frontchar = aim.front();
        if (frontchar == '*') {
          if (backchar == '*') {
            aim.erase(0);
            aim.erase(aim.end() - 1);

            for (auto &[key, val] : _connection) {
              if (key.find(aim) != std::string::npos) {
                val->send(info);
              }
            }
          } else {
            aim.erase(0);
            for (auto &[key, val] : _connection) {
              if (key.length() < aim.length()) {
                continue;
              } else {
                if (key.substr(0, aim.length()) == aim) {
                  val->send(info);
                }
              }
            }
          }
        } else {
          if (backchar == '*') {
            aim.erase(aim.end() - 1);
            for (auto &[key, val] : _connection) {
              if (key.length() < aim.length()) {
                continue;
              } else {
                if (key.substr(key.length() - aim.length(), key.length()) ==
                    aim) {
                  val->send(info);
                }
              }
            }
          } else { // todo
            auto r = _connection.find(aim);
            if (r != _connection.end()) {
            }
            auto &_c = _connection[aim];
            if (_c) {
              _connection[aim]->send(info);
            } else {
              throw std::invalid_argument("Invalid Aim!");
            }
          }
        }
      } else {
        throw std::invalid_argument("Can't find aim from body!");
      }
      auto returnMsg = new Message();
      returnMsg->_header._id = msg.msg._header._id;
      *returnMsg << "1";
      msg.remote->_messageOut.push_back(returnMsg);
    } catch (std::exception e) {
      auto returnMsg = new Message();
      returnMsg->_header._id = msg.msg._header._id;
      *returnMsg << "0"
                 << "Err";
      msg.remote->_messageOut.push_back(returnMsg);
    }
    msg.remote->writeMessage();
  }
  WS_PATH_LIST_BEGIN
  WS_PATH_ADD("/");
  WS_PATH_LIST_END
private:
  std::unordered_map<std::string, WebSocketConnectionPtr> _connection;
  std::mutex _m;
};

class Server {
protected:
  Queue<OwnedMessage, false> _messageIn;
  std::vector<std::shared_ptr<Connection>> _connections;
  asio::io_context _asioContext;
  std::thread _threadContext;
  std::thread _threadWebsocket;
  asio::ip::tcp::acceptor _asioAcceptor;
  std::shared_ptr<WebSock> _websock{std::make_shared<WebSock>()};
  uint32_t _IDCounter{10000};

public:
  std::deque<std::function<void()>> _task;
  std::condition_variable _cvBlocking;
  std::mutex _muxBlocking;
  std::mutex _muxQueue;

public:
  void postTaskToMainThread(std::function<void()> f) {
    std::scoped_lock lock(_muxQueue);
    _task.emplace_back(f);
    _cvBlocking.notify_one();
  }
  void notifyMainThread() { _cvBlocking.notify_one(); }

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
      _threadWebsocket = std::thread([this]() {
        drogon::app()
            .addListener("0.0.0.0", 6868)
            .setThreadNum(4)
            .registerController(_websock)
            .run();
      });
      while (true) {
        while (!_messageIn.empty()) {
          _websock->sendMessageToWebSocket(_messageIn.pop_front());
        }
        if (!_task.empty()) {
          std::scoped_lock lock(_muxQueue);
          do {
            _task.front()();
            _task.pop_front();
          } while (!_task.empty());
        }
        std::unique_lock<std::mutex> ul(_muxBlocking);
        _cvBlocking.wait(ul);
      }
      return -1;
    } catch (std::exception &e) {
      LOG(WARNING) << e.what();
      return -1;
    }
  }
  void waitForClientConnection() {
    _asioAcceptor.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
          if (!ec) {
            _connections.push_back(std::make_shared<Connection>(
                _asioContext, std::move(socket), _messageIn));
            _connections.back()->startClient(_IDCounter++);
          } else {
            LOG(WARNING) << ec.message();
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
    LOG(INFO) << "An Connection DisConnect,Now Connection Number:"
              << std::to_string(_connections.size());
  }
};
static void PostToMainThread(std::function<void()> f) {
  server->postTaskToMainThread(f);
}
static void ClearThisConnection(std::shared_ptr<Connection> c) {
  server->clearConnection(c);
}
static void NotifyMainThread() { server->notifyMainThread(); }
} // namespace Silenced
