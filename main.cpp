#include "zyyservercommon.h"
#include <iterator>

int main() {
  // Silenced::Server s(6767);
  // s.start();

  std::vector<int> arr{};
  auto x =std::find(std::begin(arr),arr.end(),1);

  // arr.erase(std::begin(arr));

 // std::cout<<arr.front()<<std::endl;


  return 0;
}