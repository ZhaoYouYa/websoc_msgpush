#pragma once
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <deque>
#include <drogon/HttpAppFramework.h>
#include <functional>
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
#include <drogon/HttpTypes.h>
#include <drogon/WebSocketConnection.h>
#include <drogon/WebSocketController.h>
#include <drogon/utils/FunctionTraits.h>
#include <tuple>
#include <unordered_map>

#include <boost/log/attributes.hpp>
#include <boost/log/attributes/named_scope.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
using namespace drogon;

namespace Log {
enum class LogType { Console, File };
typedef boost::log::sinks::synchronous_sink<
    boost::log::sinks::text_file_backend>
    file_sink;
static boost::log::sources::severity_logger<boost::log::trivial::severity_level>
    _logger;
static void Init(LogType type, int level, int maxFileSize = 1024,
                 int maxBackupIndex = 1) {
  boost::log::formatter formatter =
      boost::log::expressions::stream
      << "["
      << boost::log::expressions::format_date_time<boost::posix_time::ptime>(
             "TimeStamp", "%Y-%m-%d %H:%M:%S.%f") /*.%f*/
      << "|"
      << boost::log::expressions::attr<
             boost::log::attributes::current_thread_id::value_type>("ThreadID")
      << "]["
      << boost::log::expressions::attr<boost::log::trivial::severity_level>(
             "Severity")
      << "]" << boost::log::expressions::smessage << "    "
      << boost::log::expressions::format_named_scope(
             "Scope", boost::log::keywords::format = "(%f:%l)",
             boost::log::keywords::iteration = boost::log::expressions::reverse,
             boost::log::keywords::depth = 1);

  switch (type) {
  case LogType::Console: {
    auto consoleSink = boost::log::add_console_log();
    consoleSink->set_formatter(formatter);
    boost::log::core::get()->add_sink(consoleSink);
  } break;
  case LogType::File: {
    boost::shared_ptr<file_sink> fileSink(new file_sink(
        boost::log::keywords::target_file_name =
            "%Y%m%d_%H%M%S_%N.log", // file name pattern
        boost::log::keywords::time_based_rotation =
            boost::log::sinks::file::rotation_at_time_point(16, 0, 0),
        boost::log::keywords::rotation_size =
            maxFileSize * 1024 * 1024, // rotation size, in characters
        boost::log::keywords::open_mode = std::ios::out | std::ios::app));

    fileSink->locked_backend()->set_file_collector(
        boost::log::sinks::file::make_collector(
            boost::log::keywords::target = "logs", // folder name.
            boost::log::keywords::max_size =
                maxFileSize * maxBackupIndex * 1024 *
                1024, // The maximum amount of space of the folder.
            boost::log::keywords::min_free_space =
                10 * 1024 * 1024, // Reserved disk space minimum.
            boost::log::keywords::max_files = 512));

    fileSink->set_formatter(formatter);
    fileSink->locked_backend()->scan_for_files();
    fileSink->locked_backend()->auto_flush(true);
    boost::log::core::get()->add_sink(fileSink);
  } break;
  default: {
    auto consoleSink = boost::log::add_console_log();
    consoleSink->set_formatter(formatter);
    boost::log::core::get()->add_sink(consoleSink);
  } break;
  }
  boost::log::add_common_attributes();
  boost::log::core::get()->add_global_attribute(
      "Scope", boost::log::attributes::named_scope());
  boost::log::core::get()->set_filter(boost::log::trivial::severity >= level);
}

}; // namespace Log

#define SLOG_TRACE(logEvent)                                                   \
  BOOST_LOG_FUNCTION();                                                        \
  BOOST_LOG_SEV(Log::_logger, boost::log::trivial::trace) << logEvent;

#define SLOG_DEBUG(logEvent)                                                   \
  BOOST_LOG_FUNCTION();                                                        \
  BOOST_LOG_SEV(Log::_logger, boost::log::trivial::debug) << logEvent;

#define SLOG_INFO(logEvent)                                                    \
  BOOST_LOG_FUNCTION();                                                        \
  BOOST_LOG_SEV(Log::_logger, boost::log::trivial::info) << logEvent;

#define SLOG_WARN(logEvent)                                                    \
  BOOST_LOG_FUNCTION();                                                        \
  BOOST_LOG_SEV(Log::_logger, boost::log::trivial::warning) << logEvent;

#define SLOG_ERROR(logEvent)                                                   \
  BOOST_LOG_FUNCTION();                                                        \
  BOOST_LOG_SEV(Log::_logger, boost::log::trivial::error) << logEvent;

#define SLOG_FATAL(logEvent)                                                   \
  BOOST_LOG_FUNCTION();                                                        \
  BOOST_LOG_SEV(Log::_logger, boost::log::trivial::fatal) << logEvent;

namespace Silenced {

static void PostToMainThread(std::function<void()> f);
class Server;
class Connection;
static Server *server{nullptr};
static void ClearThisConnection(std::shared_ptr<Connection>);

class WebSock : public WebSocketController<WebSock, false> {
public:
  WebSock(){};

public:
  virtual void handleNewMessage(const WebSocketConnectionPtr &, std::string &&,
                                const WebSocketMessageType &) override{
      // DO NOTHING

  };
  virtual void
  handleNewConnection(const HttpRequestPtr &Request,
                      const WebSocketConnectionPtr &WebSocPtr) override {
    auto id = Request->parameters().find("id")->second;
    _connection[id] = WebSocPtr;
  };
  virtual void
  handleConnectionClosed(const WebSocketConnectionPtr &WebSocPtr) override {
    std::erase_if(_connection, [&WebSocPtr](auto &it) -> bool {
      auto const &[key, value] = it;
      if (value == WebSocPtr) {
        return true;
      }
      return false;
    });
  };

  WS_PATH_LIST_BEGIN
  WS_PATH_ADD("/");
  WS_PATH_LIST_END
public:
  std::unordered_map<std::string, WebSocketConnectionPtr> _connection;
};

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
  Queue<std::unique_ptr<Message>, false> _messageOut;
  std::unique_ptr<Message> _msgTemporaryOut{nullptr};
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
    asio::async_read(
        _socket, asio::buffer(_pwd, sizeof(_pwd)),
        [this](std::error_code ec, std::size_t length) {
          if (!ec) {
            std::string_view pwd(_pwd);
            std::cout << "Pwd: " << pwd << std::endl;
            if (pwd == "zyy123") {

              readMessage();
            } else {
              SLOG_WARN(_socket.local_endpoint().address().to_string());
              _socket.close();
              PostToMainThread(
                  [this]() { ClearThisConnection(this->shared_from_this()); });
            }

          } else {
            // Err occur
            SLOG_WARN(_socket.local_endpoint().address().to_string());
            _socket.close();
            PostToMainThread(
                [this]() { ClearThisConnection(this->shared_from_this()); });
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
              asio::async_read(
                  _socket,
                  asio::buffer(_msgTemporaryIn._body.data(),
                               _msgTemporaryIn._header._size),
                  [this](std::error_code ec, std::size_t length) {
                    if (!ec) {
                      OwnedMessage msg;
                      msg.msg = _msgTemporaryIn;
                      msg.remote = this->shared_from_this();
                      _messageIn.push_back(msg);
                      readMessage();
                    } else {
                      SLOG_WARN(_socket.local_endpoint().address().to_string() +
                                ec.message());
                      _socket.close();
                      PostToMainThread([this]() {
                        ClearThisConnection(this->shared_from_this());
                      });
                    }
                  });
            } else {
              readMessage();
            }
          } else {
            SLOG_WARN(_socket.local_endpoint().address().to_string() +
                      ec.message());
            _socket.close();
            PostToMainThread(
                [this]() { ClearThisConnection(this->shared_from_this()); });
          }
        });
  }
  // Thread Safe
  void writeMessage() {
    if (_msgTemporaryOut.get() != nullptr || _messageOut.empty()) {
      return;
    }
    _msgTemporaryOut = _messageOut.pop_front();

    asio::async_write(
        _socket,
        asio::buffer(&_msgTemporaryOut->_header, sizeof(MessageHeader)),
        [this](std::error_code ec, std::size_t lenght) {
          if (!ec) {
            if (_msgTemporaryOut->_body.size() > 0) {

              asio::async_write(
                  _socket,
                  asio::buffer(_msgTemporaryOut->_body.data(),
                               _msgTemporaryOut->_body.size()),
                  [this](std::error_code ec, std::size_t length) {
                    if (!ec) {
                      _msgTemporaryOut.reset(nullptr);
                      writeMessage();
                    } else {
                      SLOG_WARN(_socket.local_endpoint().address().to_string() +
                                ec.message());
                    }
                  });
            } else {
              _msgTemporaryOut.reset(nullptr);
              writeMessage();
            }
          } else {
            // write Err
            SLOG_WARN(_socket.local_endpoint().address().to_string() +
                      ec.message());
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
      _threadWebsocket = std::thread([this]() {
        drogon::app()
            .addListener("0.0.0.0", 6868)
            .setThreadNum(4)
            .registerController(_websock)
            .run();
      });
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
      SLOG_WARN(e.what());
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
            SLOG_WARN(ec.message());
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
