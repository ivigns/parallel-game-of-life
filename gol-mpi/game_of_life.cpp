#include <cassert>
#include <iostream>
#include <fstream>
#include <random>
#include <utility>

#include "mpi.h"
#include "game_of_life.hpp"

enum MpiGolTag : int {
  Start,
  Run,
  Stop,
  Update,
  Quit,
  FieldSize,
  Field,
  Running
};

GameOfLife::GameOfLife(const std::string& rules)
    : iterations_count_(0),
      desired_iterations_count_(0),
      running_(false),
      up_to_date_(true) {
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

  if (world_rank_ == 0) {
    std::mt19937 rng(1337);
    std::bernoulli_distribution bern(0.5);
    for (size_t i = 0; i < h_size; ++i) {
      field_.push_back(CyclicVector<char>());
      for (size_t j = 0; j < v_size; ++j) {
        field_[i].push_back(bern(rng));
      }
    }
  }

  BroadcastField();
  new_field_ = field_;

  return true;
}

bool GameOfLife::Start(const std::string& filename) {
  if (!field_.empty()) {
    return false;
  }

  if (world_rank_ == 0) {
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
  }

  BroadcastField();
  new_field_ = field_;

  return true;
}

bool GameOfLife::Run(const size_t add_iterations) {
  if (field_.empty()) {
    return false;
  }

  if (world_rank_ == 0) { // мастер
    size_t message = add_iterations;
    for (int i = 1; i < world_size_; ++i) {
      MPI_Send(&message, 1, MPI_UNSIGNED_LONG, i, MpiGolTag::Run, mpi_comm_);
    }
    up_to_date_ = false;
  }
  // Обработка сообщений для раба была сделана заранее.
  desired_iterations_count_ += add_iterations;
  running_ = true;

  return true;
}

bool GameOfLife::Stop(const size_t final_iterations_count) {
  if (field_.empty()) {
    return false;
  }
  if (!Running()) {
    return true;
  }

  if (world_rank_ == 0) { // мастер
    char x = 1;
    for (int i = 1; i < world_size_; ++i) { // Посылаем сигнал об остановке.
      MPI_Send(&x, 1, MPI_BYTE, i, MpiGolTag::Stop, mpi_comm_);
    }
    size_t message;
    size_t max_iterations_count = 0;
    for (int i = 1; i < world_size_; ++i) { // Получаем текущее число итераций.
      MPI_Recv(&message, 1, MPI_UNSIGNED_LONG, i, MpiGolTag::Stop, mpi_comm_,
          MPI_STATUS_IGNORE);
      max_iterations_count = std::max(message, max_iterations_count);
    }
    ++max_iterations_count;
    // Посылаем число итераций для доделывания.
    for (int i = 1; i < world_size_; ++i) {
      MPI_Send(&max_iterations_count, 1, MPI_UNSIGNED_LONG, i,
          MpiGolTag::Stop, mpi_comm_);
    }
    desired_iterations_count_ = max_iterations_count;
    for (int i = 1; i < world_size_; ++i) { // Ожидаем завершения работы.
      MPI_Recv(&x, 1, MPI_BYTE, i, MpiGolTag::Stop, mpi_comm_,
          MPI_STATUS_IGNORE);
    }
  } else {
    // Обработка сообщений для раба была сделана заранее.
    desired_iterations_count_ = final_iterations_count;
  }
  running_ = false;

  return true;
}

bool GameOfLife::Update() {
  if (field_.empty()) {
    return false;
  }
  if (Running()) {
    return false;
  }
  if (up_to_date_) {
    return true;
  }

  if (world_rank_ == 0) {
    char x = 1;
    for (int i = 1; i < world_size_; ++i) { // Посылаем сигнал об обновлении.
      MPI_Send(&x, 1, MPI_BYTE, i, MpiGolTag::Update, mpi_comm_);
    }
    for (int i = 1; i < world_size_; ++i) { // Собираем поле по кусочкам.
      for (long long j = borders_[i]; j < borders_[i + 1]; ++j) {
        MPI_Recv(field_[j].data(), static_cast<int>(field_[0].size()),
            MPI_BYTE, i, MpiGolTag::Update, mpi_comm_, MPI_STATUS_IGNORE);
      }
    }
  }

  up_to_date_ = true;
  return true;
}

void GameOfLife::Quit() {
  assert(world_rank_ == 0);
  Stop();
  Update();

  char x = 1;
  for (int i = 1; i < world_size_; ++i) {
    MPI_Send(&x, 1, MPI_BYTE, i, MpiGolTag::Quit, mpi_comm_);
  }
}

void GameOfLife::PrintField(std::ostream& out) {
  if (field_.empty()) {
    out << "No field has been created yet.\n";
    return;
  }

  Update();
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

bool GameOfLife::PrintStatus(std::ostream& out) {
  if (field_.empty()) {
    out << "No field has been created yet.\n";
    return false;
  }
  if (Running()) {
    out << "Running...\n";
    return false;
  }

  out << "Stopped at " << desired_iterations_count_ << " iteration.\n";

  return true;
}

void GameOfLife::SetMpiCommunicator(const MPI_Comm& mpi_comm) {
  mpi_comm_ = mpi_comm;
  int world_size;
  MPI_Comm_size(mpi_comm_, &world_size);
  world_size_ = world_size;
  int world_rank;
  MPI_Comm_rank(mpi_comm_, &world_rank);
  world_rank_ = world_rank;
}

void GameOfLife::SlaveSynchronize() {
  assert(world_rank_ != 0);
  char x = 1;
  MPI_Status status;
  MPI_Recv(&x, 1, MPI_BYTE, 0, MPI_ANY_TAG, mpi_comm_, &status);
  if (status.MPI_TAG == MpiGolTag::Quit) {
    return;
  }
  assert(status.MPI_TAG == MpiGolTag::Start);
  Start();

  bool quit = false;
  bool notify_master = false;
  bool turn = static_cast<bool>(world_rank_ % 2); // Для предотвращения дедлока.
  // Флажки актуальности правой и левой границы.
  bool border_gained[2] = {false, false};

  while (!quit) {
    if (running_ && iterations_count_ < desired_iterations_count_) {
      if (turn) {
        MPI_Send(field_[borders_[0]].data(), static_cast<int>(field_[0].size()),
            MPI_BYTE, process_[world_rank_ - 2], MpiGolTag::Run, mpi_comm_);
        MPI_Send(field_[borders_.back() - 1].data(), static_cast<int>(field_[0].size()),
            MPI_BYTE, process_[world_rank_], MpiGolTag::Run, mpi_comm_);
        std::cout << world_rank_ << " sent.\n";
        while (!(border_gained[0] && border_gained[1])) {
          SlaveRecv(border_gained, quit, notify_master);
        }
        std::cout << world_rank_ << " received.\n";
      } else {
        while (!(border_gained[0] && border_gained[1])) {
          SlaveRecv(border_gained, quit, notify_master);
        }
        std::cout << world_rank_ << " received.\n";
        MPI_Send(field_[borders_[0]].data(), static_cast<int>(field_[0].size()),
            MPI_BYTE, process_[world_rank_ - 2], MpiGolTag::Run, mpi_comm_);
        MPI_Send(field_[borders_.back() - 1].data(), static_cast<int>(field_[0].size()),
            MPI_BYTE, process_[world_rank_], MpiGolTag::Run, mpi_comm_);
        std::cout << world_rank_ << " sent.\n";
      }

      CalculatePart();
      field_.swap(new_field_);
      ++iterations_count_;
      border_gained[0] = border_gained[1] = false;
      if (iterations_count_ >= desired_iterations_count_) {
        running_ = false;
        if (notify_master) {
          MPI_Send(&x, 1, MPI_BYTE, 0, MpiGolTag::Stop, mpi_comm_);
          notify_master = false;
        }
      }
    } else {
      std::cout << world_rank_ << " ready.\n";
      SlaveRecv(border_gained, quit, notify_master);
    }
  }
}

void GameOfLife::SlaveRecv(bool* border_gained, bool& quit,
                           bool& notify_master) {
  MPI_Status status;
  MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, mpi_comm_, &status);
  if (status.MPI_SOURCE == 0) {
    if (status.MPI_TAG == MpiGolTag::Running) {
      char x = 1;
      MPI_Recv(&x, 1, MPI_BYTE, 0, MpiGolTag::Running, mpi_comm_,
          MPI_STATUS_IGNORE);
      MPI_Send(&running_, 1, MPI_CXX_BOOL, 0, MpiGolTag::Running, mpi_comm_);

    } else if (status.MPI_TAG == MpiGolTag::Run) {
      size_t add_iterations;
      MPI_Recv(&add_iterations, 1, MPI_UNSIGNED_LONG, 0, MpiGolTag::Run,
          mpi_comm_, MPI_STATUS_IGNORE);
      Run(add_iterations);

    } else if (status.MPI_TAG == MpiGolTag::Stop) {
      char x = 1;
      MPI_Recv(&x, 1, MPI_BYTE, 0, MpiGolTag::Stop, mpi_comm_,
          MPI_STATUS_IGNORE);
      MPI_Send(&iterations_count_, 1, MPI_UNSIGNED_LONG, 0, MpiGolTag::Stop,
          mpi_comm_);
      MPI_Recv(&desired_iterations_count_, 1, MPI_UNSIGNED_LONG, 0,
          MpiGolTag::Stop, mpi_comm_, MPI_STATUS_IGNORE);
      notify_master = true;

    } else if (status.MPI_TAG == MpiGolTag::Update) {
      char x = 1;
      MPI_Recv(&x, 1, MPI_BYTE, 0, MpiGolTag::Update, mpi_comm_,
          MPI_STATUS_IGNORE);
      for (long long j = borders_[0]; j < borders_.back(); ++j) {
        MPI_Recv(field_[j].data(), static_cast<int>(field_[0].size()),
            MPI_BYTE, 0, MpiGolTag::Update, mpi_comm_, MPI_STATUS_IGNORE);
      }

    } else if (status.MPI_TAG == MpiGolTag::Quit) {
      char x = 1;
      MPI_Recv(&x, 1, MPI_BYTE, 0, MpiGolTag::Quit, mpi_comm_,
          MPI_STATUS_IGNORE);
      quit = true;
    }

  } else {
    assert(status.MPI_TAG == MpiGolTag::Run);
    if (status.MPI_SOURCE == process_[world_rank_]) { // правая граница
      MPI_Recv(field_[borders_.back()].data(),
          static_cast<int>(field_[0].size()), MPI_BYTE, status.MPI_SOURCE,
          MpiGolTag::Run, mpi_comm_, MPI_STATUS_IGNORE);
      border_gained[1] = true;
    } else if (status.MPI_SOURCE == process_[world_rank_ - 2]) {// левая граница
      MPI_Recv(field_[borders_[0] - 1].data(),
          static_cast<int>(field_[0].size()), MPI_BYTE, status.MPI_SOURCE,
          MpiGolTag::Run, mpi_comm_, MPI_STATUS_IGNORE);
      border_gained[0] = true;
    }
  }
}

void GameOfLife::CalculatePart() {
  for (long long i = borders_[0]; i < borders_.back(); ++i) {
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

void GameOfLife::BroadcastField() {
  for (int i = 1; i < world_size_; ++i) {
    process_.push_back(i);
  }

  if (world_rank_ == 0) {
    char x = 1;
    for (int i = 1; i < world_size_; ++i) { // Оповещаем все процессы о старте.
      MPI_Send(&x, 1, MPI_BYTE, i, MpiGolTag::Start, mpi_comm_);
    }

    borders_.push_back(0);
    for (int i = 1; i < world_size_ + 1; ++i) {
      borders_.push_back(static_cast<long long>(field_.size() / world_size_));
      // Остаток распределяем между первыми потоками.
      if (i - 1 < field_.size() % world_size_) {
        ++borders_[i];
      }
      borders_[i] += borders_[i - 1];
    }

    for (int i = 1; i < world_size_; ++i) {
      long long size[2] = {borders_[i + 1] - borders_[i] + 2,
                           static_cast<long long>(field_[0].size())};
      MPI_Send(size, 2, MPI_LONG_LONG, i, MpiGolTag::FieldSize, mpi_comm_);
      for (long long j = borders_[i] - 1; j < borders_[i + 1] + 1; ++j) {
        MPI_Send(field_[j].data(), static_cast<int>(size[1]), MPI_BYTE, i,
            MpiGolTag::Field, mpi_comm_);
      }
    }
  } else {
    long long size[2];
    MPI_Recv(size, 2, MPI_LONG_LONG, 0, MpiGolTag::FieldSize, mpi_comm_,
        MPI_STATUS_IGNORE);
    for (long long i = 0; i < size[0]; ++i) {
      field_.push_back(CyclicVector<char>());
      field_[i].assign(size[1], 0);
      MPI_Recv(field_[i].data(), static_cast<int>(size[1]), MPI_BYTE, 0,
          MpiGolTag::Field, mpi_comm_, MPI_STATUS_IGNORE);
    }

    borders_.push_back(1);
    borders_.push_back(size[0] - 1);
  }
}

bool GameOfLife::Running() {
  if (!running_) {
    return false;
  }

  if (world_rank_ == 0) {
    running_ = false;
    bool locally_running;
    char x = 1;
    for (int i = 1; i < world_size_; ++i) {
      MPI_Send(&x, 1, MPI_BYTE, i, MpiGolTag::Running, mpi_comm_);
      MPI_Recv(&locally_running, 1, MPI_CXX_BOOL, i, MpiGolTag::Running,
          mpi_comm_, MPI_STATUS_IGNORE);
      std::cout << "Received r_ from " << i << "\n";
      running_ = running_ || locally_running;
    }
  }

  return running_;
}
