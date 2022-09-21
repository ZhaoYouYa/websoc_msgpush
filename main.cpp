
#include "server.h"
#include <chrono>
#include <condition_variable>
#include <iterator>
#include <mutex>
#include <pthread.h>
#include <thread>
#include <unordered_map>

std::condition_variable v;
std::mutex m;
void f() {

   std::this_thread::sleep_for(std::chrono::seconds(5));
  std::cout << "Lock" << std::endl;
  std::unique_lock<std::mutex> ul(m);
  std::cout << "Lock1" << std::endl;
  v.wait(ul);
  std::cout << "Lock3" << std::endl;
  while (true) {
    std::cout << 1 << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}


class A{
public:
int a{10};
};
int main() {

 A* a1 = new A();
 a1->a = 0;

 

auto t = std::thread(f);



t.join();
v.notify_one();

  

  // Silenced::Server s(6767);
  // s.start();

  // std::vector<int> arr{};
  // auto x =std::find(std::begin(arr),arr.end(),1);

  // arr.erase(std::begin(arr));

  // std::cout<<arr.front()<<std::endl;

  return 0;
}