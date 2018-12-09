#include <cassert>
#include <iostream>
#include <fstream>
#include <random>
#include <utility>

#include "game_of_life.hpp"

GameOfLife::GameOfLife(const size_t num_threads, const std::string& rules)
    : permission_(false),
      barrier_(num_threads, sync_mutex_, all_threads_stopped_, permission_),
      iterations_count_(0),
      desired_iterations_count_(0),
      running_(false),
      quitting_(false),
      num_threads_(num_threads),
      master_thread_(nullptr) {
  bool set_born = true;
  for (const auto& c : rules) {
    if (c == 'b' || c == 'B') {
      set_born = true;
    } else if (c == 's' || c == 'S') {
      set_born = false;
    } else if (c == '/') {
      set_born = !set_born;
      continue;
    } else if (c >= '0' && c <= '9') {
      if (set_born) {
        rules_.born_.push_back(c - '0');
      } else {
        rules_.stay_.push_back(c - '0');
      }
    }
  }

  // При плохих входных данных устанавливаем b3/s23.
  if (rules_.born_.empty() && rules_.stay_.empty()) {
    rules_.born_.push_back(3);
    rules_.stay_.push_back(2);
    rules_.stay_.push_back(3);
  }
}

bool GameOfLife::Start(const size_t h_size, const size_t v_size) {
  if (!field_.empty()) {
    return false;
  }

  std::mt19937 rng(1337);
  std::bernoulli_distribution bern(0.5);
  for (size_t i = 0; i < h_size; ++i) {
    field_.push_back(CyclicVector<char>());
    for (size_t j = 0; j < v_size; ++j) {
      field_[i].push_back(bern(rng));
    }
  }
  new_field_ = field_;

  CreateThreads();

  return true;
}

bool GameOfLife::Start(const std::string& filename) {
  if (!field_.empty()) {
    return false;
  }

  std::ifstream fin(filename);
  int c = 0;
  for (size_t i = 0; !fin.eof(); ++i) {
    for (size_t j = 0; !fin.eof(); ++j) {
      c = fin.get();
      if (fin.eof()) {
        break;
      }
      if (c == ',') {
        continue;
      }
      if (c == '\n') {
        break;
      }

      if (j == 0) {
        field_.push_back(CyclicVector<char>());
      }
      field_[i].push_back(static_cast<char>(c - '0'));
    }
  }
  new_field_ = field_;

  CreateThreads();

  return true;
}

bool GameOfLife::Run(const size_t add_iterations) {
  status_lock_.ReaderLock();
  if (field_.empty()) {
    status_lock_.ReaderUnlock();
    return false;
  }
  if (running_) {
    status_lock_.ReaderUnlock();
    return false;
  }
  status_lock_.ReaderUnlock();

  status_lock_.WriterLock();
  desired_iterations_count_ += add_iterations;
  running_ = true;
  {
    std::unique_lock<std::mutex> lock(master_sync_mutex_);
    new_task_received_.notify_one();
  }
  status_lock_.WriterUnlock();

  return true;
}

bool GameOfLife::Stop() {
  status_lock_.ReaderLock();
  if (field_.empty()) {
    status_lock_.ReaderUnlock();
    return false;
  }
  if (!running_) {
    status_lock_.ReaderUnlock();
    return true;
  }
  status_lock_.ReaderUnlock();

  std::unique_lock<std::mutex> lock(master_sync_mutex_);
  status_lock_.WriterLock();
  desired_iterations_count_ = iterations_count_ + 1;
  status_lock_.WriterUnlock();
  while (running_) {
    new_task_received_.wait(lock);
  }
  return true;
}

void GameOfLife::Quit() {
  Stop();
  status_lock_.WriterLock();
  quitting_ = true;
  running_ = true;
  {
    std::unique_lock<std::mutex> lock(master_sync_mutex_);
    new_task_received_.notify_one();
  }
  status_lock_.WriterUnlock();

  for (auto& thread : threads_) {
    thread.join();
  }
  if (master_thread_ != nullptr) {
    master_thread_->join();
  };
  // Костыли для нормального отображения.
  running_ = false;
  field_.swap(new_field_);
  --iterations_count_;
}

void GameOfLife::PrintField(std::ostream& out) const {
  if (field_.empty()) {
    out << "No field has been created yet.\n";
    return;
  }

  out << "Field:\n\u2554";
  for (int i = 0; i < field_[0].size(); ++i) {
    out << "\u2550";
  }
  out << "\u2557\n";
  for (const auto& line : field_) {
    out << "\u2551";
    for (const auto& cell : line) {
      out << (cell ? "\u2588" : "\u2591");
    }
    out << "\u2551\n";
  }
  out << "\u255A";
  for (int i = 0; i < field_[0].size(); ++i) {
    out << "\u2550";
  }
  out << "\u255D\n";
}

bool GameOfLife::PrintStatus(std::ostream& out) const {
  if (field_.empty()) {
    out << "No field has been created yet.\n";
    return false;
  }

  status_lock_.ReaderLock();
  if (running_) {
    out << "Running... Currently at " << iterations_count_ << " iteration.\n"
        << "To show the field calculations should be stopped.\n";
    status_lock_.ReaderUnlock();
    return false;
  }

  out << "Stopped at " << iterations_count_ << " iteration.\n";

  status_lock_.ReaderUnlock();
  return true;
}

void GameOfLife::CreateThreads() {
  num_threads_ = std::min(num_threads_, field_.size());
  assert(barrier_.ResizeBarrier(num_threads_));
  borders_.push_back(0);
  for (size_t i = 1; i < num_threads_ + 1; ++i) {
    borders_.push_back(static_cast<long long>(field_.size() / num_threads_));
    // Остаток распределяем между первыми потоками.
    if (i - 1 < field_.size() % num_threads_) {
      ++borders_[i];
    }
    borders_[i] += borders_[i - 1];
  }

  for (size_t i = 0; i < num_threads_; ++i) {
    threads_.push_back(std::move(std::thread(&GameOfLife::Synchronize,
        this, i)));
  }
  master_thread_ = std::make_unique<std::thread>(
      &GameOfLife::MasterSynchronize, this);
}

void GameOfLife::Synchronize(const size_t thread_id) {
  while (true) {
    bool calculate = false;
    status_lock_.ReaderLock();
    if (quitting_) {
      status_lock_.ReaderUnlock();
      break;
    }
    if (running_ && iterations_count_ < desired_iterations_count_) {
      calculate = true;
    }
    status_lock_.ReaderUnlock();

    if (calculate) {
      CalculatePart(thread_id);
    }

    barrier_.PassThrough();
  }
}

void GameOfLife::CalculatePart(const size_t thread_id) {
  for (long long i = borders_[thread_id]; i < borders_[thread_id + 1]; ++i) {
    for (long long j = 0; j < field_[i].size(); ++j) {
      char num_alive = 0;
      for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
          if (x != 0 || y != 0) {
            num_alive += field_[i + x][j + y];
          }
        }
      }

      if (field_[i][j]) {
        new_field_[i][j] = 0;
        for (auto num_to_stay : rules_.stay_) {
          if (num_alive == num_to_stay) {
            new_field_[i][j] = 1;
            break;
          }
        }
      } else {
        new_field_[i][j] = 0;
        for (auto num_to_be_born : rules_.born_) {
          if (num_alive == num_to_be_born) {
            new_field_[i][j] = 1;
            break;
          }
        }
      }
    }
  }
}

void GameOfLife::MasterSynchronize() {
  while (true) {
    status_lock_.ReaderLock();
    if (quitting_) {
      status_lock_.ReaderUnlock();
      return;
    }
    {
      std::unique_lock<std::mutex> outer_lock(master_sync_mutex_);
      new_task_received_.notify_one();
      while (!running_) {
        status_lock_.ReaderUnlock();
        new_task_received_.wait(outer_lock);
        status_lock_.ReaderLock();
      }
    }
    status_lock_.ReaderUnlock();

    {
      std::unique_lock<std::mutex> lock(sync_mutex_);
      permission_ = true;
      all_threads_stopped_.notify_one();
      status_lock_.ReaderLock();
      while (permission_ && !quitting_) {
        status_lock_.ReaderUnlock();
        all_threads_stopped_.wait(lock);
        status_lock_.ReaderLock();
      }
      status_lock_.ReaderUnlock();
    }

    status_lock_.WriterLock();
    field_.swap(new_field_);
    ++iterations_count_;
    if (iterations_count_ >= desired_iterations_count_) {
      running_ = false;
    }
    status_lock_.WriterUnlock();
  }
}