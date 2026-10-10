#pragma once
// Native tests use a real mutex: a no-op lock would hide torn state snapshots.
#ifdef _WIN32
  #include <windows.h>
typedef SRWLOCK portMUX_TYPE;
  #define portMUX_INITIALIZER_UNLOCKED SRWLOCK_INIT
  #define portENTER_CRITICAL(lock) AcquireSRWLockExclusive(lock)
  #define portEXIT_CRITICAL(lock) ReleaseSRWLockExclusive(lock)
#else
  #include <pthread.h>
typedef pthread_mutex_t portMUX_TYPE;
  #define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
  #define portENTER_CRITICAL(lock) pthread_mutex_lock(lock)
  #define portEXIT_CRITICAL(lock) pthread_mutex_unlock(lock)
#endif
