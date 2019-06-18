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

  void connect();

public:
  /// Must be called from main GUI thread
  Dispatcher();
  ~Dispatcher();

  /// Queue function to main GUI thread.
  /// Can be called from any thread.
  template <typename T>
  void post(T &&function) {
    std::lock_guard<std::mutex> lock(functions_mutex);
    functions.emplace_back(std::forward<T>(function));
    dispatcher();
  }

  /// Must be called from main GUI thread
  void disconnect();

  /// Must be called from main GUI thread
  void reset();
};
