#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include <vector>

#include "multithreading_utils.hpp"
#include "cyclic_vector.hpp"

class GameOfLife {
 public:
  // Конструктор от числа потоков и правил игры.
  explicit GameOfLife(const size_t num_threads = 4,
                      const std::string& rules = "b3/s23");

  // Создание поля h_size x v_size с рандомными значениями.
  bool Start(const size_t h_size, const size_t v_size);

  // Загрузка поля из .csv файла.
  bool Start(const std::string& filename);

  // Запуск процесса выполнения нескольких итераций перерасчета поля.
  bool Run(const size_t num_iterations);

  // Досрочная остановка вычислений.
  bool Stop();

  // Остановка всех вычислений и завершение потоков.
  void Quit();

  // Вывод всего поля. Можно использовать только когда заранее известно,
  // что все потоки остановлены.
  void PrintField(std::ostream& out = std::cout) const;

  // Состояние класса. В случае бездействия потоков возвращает true.
  bool PrintStatus(std::ostream& out = std::cout) const;

 private:
  void CreateThreads();

  // Функции, которые будут выполняться потоками.

  // Процесс синхронизации между потоками.
  void Synchronize(const size_t thread_id);

  // Содержательная часть игры "Жизнь".
  void CalculatePart(const size_t thread_id);

  // Операция потока, отвечающего за обновление статуса всей игры.
  void MasterSynchronize();

 private:
  CyclicVector<CyclicVector<char>> field_;
  CyclicVector<CyclicVector<char>> new_field_;

  std::mutex sync_mutex_; // Синхронизирует работу барьера с главным потоком.
  std::condition_variable all_threads_stopped_;
  bool permission_;
  CyclicBarrier barrier_;

  mutable ReaderWriterLock status_lock_;   // Отвечает за статус игры.
  size_t iterations_count_;
  size_t desired_iterations_count_;
  bool running_;
  bool quitting_;

  size_t num_threads_;  // Число потоков, работающих с полем.
  std::vector<long long> borders_;  // Границы участков поля для потоков.
  std::vector<std::thread> threads_;  // "Рабы на плантации".
  std::unique_ptr<std::thread> master_thread_;   // "Надсмотрщик".
  // Синхронизирует работу главного потока с поступающими сообщениями.
  std::mutex master_sync_mutex_;
  std::condition_variable new_task_received_;

  struct Rules {  // Правила игры. Числа окружающих живых ячеек, при которых
    std::vector<char> born_;  // ячейка рождается,
    std::vector<char> stay_;  // ячейка остается жить.
  };
  Rules rules_;
};