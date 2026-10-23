#ifndef WRL_FIX_H
#define WRL_FIX_H
#include <memory>
#include <windows.h>

  namespace Microsoft {
      namespace WRL {
          template <typename T> using ComPtr = std::shared_ptr<T>;
          template <typename T> struct RuntimeClass { static T* Create() { return nullptr; } };
          template <typename T> struct Implements {};
          struct ClassicCom {};
          template <typename T, typename... Args> struct RuntimeClassFlags {};

          // Adding a helper for the .As() method used in the code
          template <typename T, typename U>
          T* As(U* p) { return (T*)p; }
      }
  }
  #endif
