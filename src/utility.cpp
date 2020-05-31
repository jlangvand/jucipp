#include "utility.hpp"

ScopeGuard::~ScopeGuard() {
  if(on_exit)
    on_exit();
}
