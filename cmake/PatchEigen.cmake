# Adds Tensor::resize(const array<OtherIndex, N>&) to Eigen.
# demucs.cpp builds tensors from slices declared with Eigen::array<int, N>; GCC accepts the implicit
# int -> Index conversion but Clang and MSVC (correctly) don't. This overload makes it explicit.
set(_file "${EIGEN_SRC}/unsupported/Eigen/CXX11/src/Tensor/Tensor.h")
file(READ "${_file}" _src)
string(FIND "${_src}" "WIS_PATCHED_RESIZE" _already)
if(_already EQUAL -1)
    set(_anchor "    // Why this overload, DSizes is derived from array ??? //")
    string(FIND "${_src}" "${_anchor}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "PatchEigen: anchor not found in ${_file}")
    endif()
    set(_overload "    // WIS_PATCHED_RESIZE: accept dimensions given with a different integer type
    template <typename OtherIndex, typename std::enable_if<!std::is_same<OtherIndex, Index>::value, int>::type = 0>
    EIGEN_DEVICE_FUNC void resize(const array<OtherIndex, NumIndices>& dimensions) {
      array<Index, NumIndices> dims;
      for (int i = 0; i < NumIndices; ++i) dims[i] = static_cast<Index>(dimensions[i]);
      resize(dims);
    }

")
    string(REPLACE "${_anchor}" "${_overload}${_anchor}" _src "${_src}")
    file(WRITE "${_file}" "${_src}")
    message(STATUS "Patched Eigen Tensor::resize")
endif()
