
#include "server.h"
int main(int argc, char *argv[]) {
  std::filesystem::path p("./log");
  if (!std::filesystem::exists(p)) {
    std::filesystem::create_directory(p);
  }
  FLAGS_log_dir = "./log";
  google::InitGoogleLogging(argv[0]);

  Silenced::Server s(6767);
  s.start();
}
