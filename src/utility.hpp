#pragma once
#include <functional>

class ScopeGuard {
public:
  std::function<void()> on_exit;
  ~ScopeGuard();
};
