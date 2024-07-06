#include <functional>
#include <memory>
#include <trantor/net/EventLoop.h>
#include <trantor/net/InetAddress.h>
#include <trantor/net/TcpServer.h>
#include <trantor/net/callbacks.h>
#include <trantor/utils/MsgBuffer.h>
using namespace trantor;
//赵雷这是什么
struct SLTcpConnect {
  std::unique_ptr<EventLoop> _loop{new EventLoop()};
  InetAddress _address{"0.0.0.0", 6767};
  TcpServer _server{_loop.get(), _address, "sl"};
  RecvMessageCallback _validPasswd{
      [](const TcpConnectionPtr &ptr, MsgBuffer *buf) {
        if (buf->readableBytes()) {
        }
      }};
};