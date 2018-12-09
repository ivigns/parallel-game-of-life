#pragma once

#include <condition_variable>
#include <mutex>

#include <queue>


// Справедливый RWLock.
class ReaderWriterLock {
 public:
  enum class RWLockState {
    Unlocked,
    Reader,   // reader acquired lock
    Writer    // writer acquired lock
  };

 public:
  ReaderWriterLock()
      : lock_state_(RWLockState::Unlocked),
        readers_acquired_lock_count_(0) {
  }

  void ReaderLock();

  void ReaderUnlock();

  void WriterLock();

  void WriterUnlock();

 private:
  RWLockState lock_state_;
  size_t readers_acquired_lock_count_;
  std::queue<std::pair<std::condition_variable*,
      RWLockState>> waiting_threads_;
  std::mutex mutex_;
};

// Многоразовый многопоточный барьер. После прохождения всех потоков через него
// требуется согласование с главным потоком.
class CyclicBarrier {
 public:
  explicit CyclicBarrier(const size_t num_threads, std::mutex& outer_mutex,
                         std::condition_variable& all_threads_stopped,
                         bool& permission)
      : num_threads_(num_threads),
        capacity_(num_threads),
        barrier_state_(false),
        outer_mutex_(outer_mutex),
        all_threads_stopped_(all_threads_stopped),
        permission_(permission) {
  }

  void PassThrough();

  bool ResizeBarrier(const size_t new_capacity);

 private:
  size_t num_threads_;
  size_t capacity_;
  bool barrier_state_;

  std::mutex mutex_;
  std::condition_variable all_threads_entered_;
  std::mutex& outer_mutex_;
  std::condition_variable& all_threads_stopped_;
  bool& permission_;
};