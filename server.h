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
#include <json/value.h>
#include <memory>
#include <mutex>
#include <optional>
#include <pstl/glue_algorithm_defs.h>
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
#include <drogon/HttpController.h>
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

static void PostToMainThread(std::function<void()> f_);
class Server;
class Connection;
static Server *server{nullptr};
static void ClearThisConnection(std::shared_ptr<Connection> connection_);
static void NotifyMainThread();
struct MessageHeader {
  uint32_t _id;
  uint32_t _size{0};
};

struct Message {
  MessageHeader _header{};
  std::vector<char> _body{};
  template <typename DataType>
  friend auto &operator<<(Message &msg_, const DataType &data_) {

    static_assert(std::is_standard_layout<DataType>::value,
                  "Data is too complex to be pushed into vector");
    size_t i = msg_._body.size();
    msg_._body.resize(i + sizeof(DataType));
    std::memcpy(msg_._body.data() + i, &data_, sizeof(DataType));
    msg_._header._size = msg_._body.size();
    return msg_;
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
  std::shared_ptr<Connection> _remote{nullptr};
  Message _msg;
};
template <typename T, bool IsCanWait> class Queue {
protected:
  std::mutex _mux_queue;
  std::deque<T> _deq_queue;
  std::condition_variable _cv_blocking;
  std::mutex _mux_blocking;

public:
  Queue() = default;
  Queue(const Queue<T, IsCanWait> &) = delete;
  virtual ~Queue() {}

public:
  const T &front() {
    std::scoped_lock lock(_mux_queue);
    return _deq_queue.front();
  }
  T pop_front() {
    std::scoped_lock lock(_mux_queue);
    auto t = std::move(_deq_queue.front());
    _deq_queue.pop_front();
    return t;
  }
  bool empty() {
    std::scoped_lock lock(_mux_queue);
    return _deq_queue.empty();
  }
  size_t size() {
    std::scoped_lock loc(_mux_queue);
    return _deq_queue.size();
  }
  size_t count() { return size(); }
  void clear() {
    std::scoped_lock lock(_mux_queue);
    _deq_queue.clear();
  }
  void wait() {
    if constexpr (IsCanWait) {
      while (empty()) {
        std::unique_lock<std::mutex> ul(_mux_blocking);
        _cv_blocking.wait(ul);
      }
    }
  }
  void push_back(const T &it_) {

    std::scoped_lock lock(_mux_queue);
    _deq_queue.emplace_back(std::move(it_));

    if constexpr (IsCanWait) {
      _cv_blocking.notify_one();
    }
  }
};

class Connection : public std::enable_shared_from_this<Connection> {
public:
  asio::ip::tcp::socket _socket;
  asio::io_context &_asio_context;
  Queue<Message *, false> _message_out;
  std::unique_ptr<Message> _msg_temporary_out{nullptr};
  Queue<OwnedMessage, false> &_message_in;
  Message _msg_temporary_in;
  char _pwd[6];
  uint32_t _id{0};
  std::function<void()> _clear_this_connection;

public:
  Connection(asio::io_context &asioContext, asio::ip::tcp::socket socket,
             Queue<OwnedMessage, false> &in)
      : _socket(std::move(socket)), _asio_context(asioContext),
        _message_in(in) {}

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
      asio::post(_asio_context, [this]() { _socket.close(); });
    }
  }

private:
  void readValidation() {
    asio::async_read(_socket, asio::buffer(_pwd, sizeof(_pwd)),
                     [this](std::error_code ec, std::size_t length) {
                       if (!ec) {
                         std::string_view pwd(_pwd, sizeof(_pwd));
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
        _socket, asio::buffer(&_msg_temporary_in, sizeof(MessageHeader)),
        [this](std::error_code ec, std::size_t length) {
          if (!ec) {
            if (_msg_temporary_in._header._size > 0) {
              _msg_temporary_in._body.resize(_msg_temporary_in._header._size);
              asio::async_read(_socket,
                               asio::buffer(_msg_temporary_in._body.data(),
                                            _msg_temporary_in._header._size),
                               [this](std::error_code ec, std::size_t length) {
                                 if (!ec) {
                                   OwnedMessage msg;
                                   msg._msg = _msg_temporary_in;
                                   msg._remote = this->shared_from_this();
                                   _message_in.push_back(msg);
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
    if (_msg_temporary_out.get() != nullptr || _message_out.empty()) {
      return;
    }

    _msg_temporary_out.reset(_message_out.pop_front());
    asio::async_write(
        _socket,
        asio::buffer(&_msg_temporary_out->_header, sizeof(MessageHeader)),
        [this](std::error_code ec, std::size_t lenght) {
          if (!ec) {
            if (_msg_temporary_out->_body.size() > 0) {

              asio::async_write(_socket,
                                asio::buffer(_msg_temporary_out->_body.data(),
                                             _msg_temporary_out->_body.size()),
                                [this](std::error_code ec, std::size_t length) {
                                  if (!ec) {
                                    _msg_temporary_out.reset(nullptr);
                                    writeMessage();
                                  } else {
                                    LOG(WARNING) << ec.message();
                                  }
                                });
            } else {
              _msg_temporary_out.reset(nullptr);
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
  virtual void handleNewMessage(const WebSocketConnectionPtr &web_ptr_,
                                std::string &&str_,
                                const WebSocketMessageType &) override {
    web_ptr_->send(str_);
    // DO NOTHING
  };
  virtual void
  handleNewConnection(const HttpRequestPtr &request_,
                      const WebSocketConnectionPtr &websoc_str_) override {
    auto id = request_->parameters().find("id");
    if (id != request_->parameters().end()) {
      std::scoped_lock lock(_m);
      _connection[id->second].push_back(websoc_str_);
    }
  };
  virtual void
  handleConnectionClosed(const WebSocketConnectionPtr &websoc_ptr) override {
    std::scoped_lock lock(_m);
    for (auto &it : _connection) {

      auto e = std::remove_if(it.second.begin(), it.second.end(),
                              [&websoc_ptr](const WebSocketConnectionPtr &it) {
                                if (it == websoc_ptr) {
                                  return true;
                                }
                                return false;
                              });
      it.second.erase(e, it.second.end());
      if (it.second.empty()) {
        _connection.erase(it.first);
        break;
      }
    }
  };
  void sendMessageToWebSocket(const OwnedMessage &msg_) {
    try {
      std::scoped_lock lock(_m);
      std::string info(msg_._msg._body.data(), msg_._msg._header._size);
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
        std::regex reg(",");
        std::sregex_token_iterator pos(aim.begin(), aim.end(), reg, -1);
        decltype(pos) end;
        for (; pos != end; ++pos) {
          std::vector<WebSocketConnectionPtr> &_c = _connection[pos->str()];
          for (auto &v : _c) {
            if (v) {
              v->send(info);
              // v->send(const std::string &msg)
            }
          }
        }

      } else {
        throw std::invalid_argument("Can't find aim from body!");
      }
      auto returnMsg = new Message();
      returnMsg->_header._id = msg_._msg._header._id;
      *returnMsg << "1";
      msg_._remote->_message_out.push_back(returnMsg);
    } catch (std::exception e) {
      auto returnMsg = new Message();
      returnMsg->_header._id = msg_._msg._header._id;
      *returnMsg << "0"
                 << "Err";
      msg_._remote->_message_out.push_back(returnMsg);
    }
    msg_._remote->writeMessage();
  }

  const Json::Value getConnectedInfo() {
    Json::Value connected_info;

    for (auto &v : _connection) {
      Json::Value s;
      s["Id"] = v.first;
      for (auto &c : v.second) {
        Json::Value Ip;
        Ip["Ip"] = c->peerAddr().toIp().c_str();
        s["Client"].append(Ip);
      }
      connected_info.append(s);
    }
    return connected_info;
  }

  WS_PATH_LIST_BEGIN
  WS_PATH_ADD("/");
  WS_PATH_LIST_END
private:
  std::unordered_map<std::string, std::vector<WebSocketConnectionPtr>>
      _connection;
  std::mutex _m;
};
class Server {
protected:
  Queue<OwnedMessage, false> _message_in;
  std::vector<std::shared_ptr<Connection>> _s_connection;
  asio::io_context _asio_context;
  std::thread _thread_context;
  std::thread _thread_websocket;
  asio::ip::tcp::acceptor _asio_acceptor;
  std::shared_ptr<WebSock> _websock{std::make_shared<WebSock>()};
  uint32_t _id_count{10000};

public:
  std::deque<std::function<void()>> _task;
  std::condition_variable _cv_blocking;
  std::mutex _mux_blocking;
  std::mutex _mux_queue;

public:
  void postTaskToMainThread(std::function<void()> f_) {
    std::scoped_lock lock(_mux_queue);
    _task.emplace_back(f_);
    _cv_blocking.notify_one();
  }
  void notifyMainThread() { _cv_blocking.notify_one(); }

public:
  Server(uint16_t port)
      : _asio_acceptor(_asio_context,
                       asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {
    assert(server == nullptr);
    server = this;
  }
  virtual ~Server() {}
  int start() {
    try {
      waitForClientConnection();
      _thread_context = std::thread([this]() { _asio_context.run(); });
      _thread_websocket = std::thread([this]() {
        drogon::app()
            .addListener("0.0.0.0", 6868)
            .setThreadNum(4)
            .registerController(_websock)
            .registerHandler(
                "/GetConnectedInfo",
                [this](
                    const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback) {
                  auto resp = HttpResponse::newHttpJsonResponse(
                      _websock->getConnectedInfo());
                  resp->addHeader("Access-Control-Allow-Origin", "*");
                  resp->addHeader("Access-Control-Allow-Methods", "GET, POST");
                  resp->addHeader("Access-Control-Allow-Private-Network",
                                  "true");
                  resp->addHeader("Access-Control-Allow-Credentials", "true");
                  callback(resp);
                },
                {Get})
            .run();
      });
      while (true) {
        while (!_message_in.empty()) {
          _websock->sendMessageToWebSocket(_message_in.pop_front());
        }
        if (!_task.empty()) {
          std::scoped_lock lock(_mux_queue);
          do {
            _task.front()();
            _task.pop_front();
          } while (!_task.empty());
        }
        std::unique_lock<std::mutex> ul(_mux_blocking);
        _cv_blocking.wait(ul);
      }
      return -1;
    } catch (std::exception &e) {
      LOG(WARNING) << e.what();
      return -1;
    }
  }
  void waitForClientConnection() {
    _asio_acceptor.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
          if (!ec) {
            _s_connection.push_back(std::make_shared<Connection>(
                _asio_context, std::move(socket), _message_in));
            _s_connection.back()->startClient(_id_count++);
          } else {
            LOG(WARNING) << ec.message();
          }
          waitForClientConnection();
        });
  }
  void clearConnection(std::shared_ptr<Connection> connection_) {

    auto isIt = [&connection_](std::shared_ptr<Connection> &i) {
      if (i.get() == connection_.get()) {
        return true;
      }
      return false;
    };
    auto result = std::find_if(begin(_s_connection), end(_s_connection), isIt);
    // at runtime C++ don't do any decide!
    if (result != _s_connection.end()) {
      _s_connection.erase(result);
    }
    LOG(INFO) << "An Connection DisConnect,Now Connection Number:"
              << std::to_string(_s_connection.size());
  }
};
static void PostToMainThread(std::function<void()> f_) {
  server->postTaskToMainThread(f_);
}
static void ClearThisConnection(std::shared_ptr<Connection> connection_) {
  server->clearConnection(connection_);
}
static void NotifyMainThread() { server->notifyMainThread(); }
} // namespace Silenced
