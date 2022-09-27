
#include "server.h"


int main() {



 

  Log::Init(Log::LogType::File, 0);

  Silenced::Server s(6767);
  //  SLOG_WARN("psd");
  // // // std::cout<<"sss"<<std::endl;
  s.start();

  // std::vector<int> arr{};
  // auto x =std::find(std::begin(arr),arr.end(),1);

  // arr.erase(std::begin(arr));

  // std::cout<<arr.front()<<std::endl;

  return 0;
}