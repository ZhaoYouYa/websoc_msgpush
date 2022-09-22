
#include "server.h"
#include <chrono>
#include <condition_variable>
#include <iterator>
#include <mutex>
#include <pthread.h>
#include <thread>
#include <unordered_map>
#include <vector>

std::condition_variable v;
std::mutex m;
std::vector<int> vi{1,2,3,4,5};
void f() {

  for(auto& i : vi) {
    std::cout<<i<<std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }


}


class A{
public:
int a{10};
};
int main() {



 

auto t = std::thread(f);

std::this_thread::sleep_for(std::chrono::seconds(2));
vi.erase(vi.begin()+2);



t.join();

  

  // Silenced::Server s(6767);
  // s.start();

  // std::vector<int> arr{};
  // auto x =std::find(std::begin(arr),arr.end(),1);

  // arr.erase(std::begin(arr));

  // std::cout<<arr.front()<<std::endl;

  return 0;
}