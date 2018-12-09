#pragma once

#include "multithreading_utils.hpp"

// ReaderWriterLock

void ReaderWriterLock::ReaderLock() {
  std::unique_lock<std::mutex> lock(mutex_);

  if (waiting_threads_.empty() && (lock_state_ == RWLockState::Unlocked ||
                                   lock_state_ == RWLockState::Reader)) {
    lock_state_ = RWLockState::Reader;
    ++readers_acquired_lock_count_;
    return;
  }

  std::condition_variable cond_var;
  std::condition_variable* cond_var_ptr = &cond_var;
  waiting_threads_.push(std::make_pair(cond_var_ptr,
      RWLockState::Reader));
  while ((lock_state_ != RWLockState::Unlocked &&
          lock_state_ != RWLockState::Reader) ||
         cond_var_ptr != waiting_threads_.front().first) {
    cond_var.wait(lock);
  }

  waiting_threads_.pop();
  lock_state_ = RWLockState::Reader;
  ++readers_acquired_lock_count_;
  // Будим всех спящих читателей.
  if (!waiting_threads_.empty() &&
      waiting_threads_.front().second == RWLockState::Reader) {
    waiting_threads_.front().first->notify_one();
  }
}

void ReaderWriterLock::ReaderUnlock() {
  std::lock_guard<std::mutex> lock(mutex_);

  --readers_acquired_lock_count_;
  if (readers_acquired_lock_count_ == 0) {
    lock_state_ = RWLockState::Unlocked;
    if (!waiting_threads_.empty()) {
      waiting_threads_.front().first->notify_one();
    }
  }
}

void ReaderWriterLock::WriterLock() {
  std::unique_lock<std::mutex> lock(mutex_);

  if (waiting_threads_.empty() && lock_state_ == RWLockState::Unlocked) {
    lock_state_ = RWLockState::Writer;
    return;
  }

  std::condition_variable cond_var;
  std::condition_variable* cond_var_ptr = &cond_var;
  waiting_threads_.push(std::make_pair(cond_var_ptr,
      RWLockState::Writer));
  while (lock_state_ != RWLockState::Unlocked ||
         cond_var_ptr != waiting_threads_.front().first) {
    cond_var.wait(lock);
  }

  waiting_threads_.pop();
  lock_state_ = RWLockState::Writer;
}

void ReaderWriterLock::WriterUnlock() {
  std::lock_guard<std::mutex> lock(mutex_);

  lock_state_ = RWLockState::Unlocked;
  if (!waiting_threads_.empty()) {
    waiting_threads_.front().first->notify_one();
  }
}

// CyclicBarrier

void CyclicBarrier::PassThrough() {
  std::unique_lock<std::mutex> lock(mutex_);

  --num_threads_;
  bool current_barrier_state = barrier_state_;
  while (current_barrier_state == barrier_state_ && num_threads_ != 0) {
    all_threads_entered_.wait(lock);
  }

  if (current_barrier_state == barrier_state_) {
    barrier_state_ = !barrier_state_;
    num_threads_ = capacity_;
    {
      std::unique_lock<std::mutex> outer_lock(outer_mutex_);
      permission_ = false;
      all_threads_stopped_.notify_one();
      while (!permission_) {
        all_threads_stopped_.wait(outer_lock);
      }
    }
    all_threads_entered_.notify_all();
  }
}

bool CyclicBarrier::ResizeBarrier(const size_t new_capacity) {
  std::unique_lock<std::mutex> lock(mutex_);

  if (num_threads_ < capacity_) { // Барьер используется.
    return false;
  }

  num_threads_ = capacity_ = new_capacity;
  return true;
}