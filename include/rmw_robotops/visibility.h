#ifndef RMW_ROBOTOPS__VISIBILITY_H_
#define RMW_ROBOTOPS__VISIBILITY_H_

#ifdef __cplusplus
extern "C"
{
#endif

// This logic was borrowed (then namespaced) from the examples on the gcc wiki:
//     https://gcc.gnu.org/wiki/Visibility

#if defined _WIN32 || defined __CYGWIN__
  #ifdef __GNUC__
    #define RMW_ROBOTOPS_EXPORT __attribute__ ((dllexport))
    #define RMW_ROBOTOPS_IMPORT __attribute__ ((dllimport))
  #else
    #define RMW_ROBOTOPS_EXPORT __declspec(dllexport)
    #define RMW_ROBOTOPS_IMPORT __declspec(dllimport)
  #endif
  #ifdef RMW_ROBOTOPS_BUILDING_LIBRARY
    #define RMW_ROBOTOPS_PUBLIC RMW_ROBOTOPS_EXPORT
  #else
    #define RMW_ROBOTOPS_PUBLIC RMW_ROBOTOPS_IMPORT
  #endif
  #define RMW_ROBOTOPS_PUBLIC_TYPE RMW_ROBOTOPS_PUBLIC
  #define RMW_ROBOTOPS_LOCAL
#else
  #define RMW_ROBOTOPS_EXPORT __attribute__ ((visibility("default")))
  #define RMW_ROBOTOPS_IMPORT
  #if __GNUC__ >= 4
    #define RMW_ROBOTOPS_PUBLIC __attribute__ ((visibility("default")))
    #define RMW_ROBOTOPS_LOCAL  __attribute__ ((visibility("hidden")))
  #else
    #define RMW_ROBOTOPS_PUBLIC
    #define RMW_ROBOTOPS_LOCAL
  #endif
  #define RMW_ROBOTOPS_PUBLIC_TYPE
#endif

#ifdef __cplusplus
}
#endif

#endif  // RMW_ROBOTOPS__VISIBILITY_H_
