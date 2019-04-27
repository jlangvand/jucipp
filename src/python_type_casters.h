#pragma once
#include "python_bind.h"
#include <boost/filesystem.hpp>

namespace pybind11 {
  namespace detail {
    template <>
    struct type_caster<boost::filesystem::path> {
    public:
      PYBIND11_TYPE_CASTER(boost::filesystem::path, _("str"));
      bool load(handle src, bool) {
        value = std::string(pybind11::str(src));
        return !PyErr_Occurred();
      }

      static handle cast(boost::filesystem::path src, return_value_policy, handle) {
        return pybind11::str(src.string());
      }
    };
  } // namespace detail
} // namespace pybind11
