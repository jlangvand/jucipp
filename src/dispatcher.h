#pragma once
#include <functional>
#include <gtkmm.h>
#include <list>
#include <mutex>

class Dispatcher {
private:
  std::list<std::function<void()>> functions;
  std::mutex functions_mutex;
  Glib::Dispatcher dispatcher;
  sigc::connection connection;

public:
  Dispatcher();
  ~Dispatcher();

  template <typename T>
  void post(T &&function) {
    {
      std::lock_guard<std::mutex> lock(functions_mutex);
      functions.emplace_back(std::forward<T>(function));
    }
    dispatcher();
  }

  void disconnect();
};
