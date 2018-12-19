#pragma once
#include <vector>

#include "cyclic_vector.hpp"

class GameOfLife {
 public:
  // Конструктор от правил игры.
  explicit GameOfLife(const std::string& rules = "b3/s23");

  // Создание поля h_size x v_size с рандомными значениями.
  bool Start(const size_t h_size = 10, const size_t v_size = 10);

  // Загрузка поля из .csv файла.
  bool Start(const std::string& filename);

  // Запуск процесса выполнения нескольких итераций перерасчета поля.
  bool Run(const size_t num_iterations);

  // Досрочная остановка вычислений.
  bool Stop(const size_t final_iterations_count = 0);

  // Обновление поля путем сбора кусочков с каждого процесса.
  bool Update();

  // Остановка всех вычислений и завершение процессов.
  void Quit();

  // Вывод всего поля. Можно использовать только когда заранее известно,
  // что все процессы остановлены.
  void PrintField(std::ostream& out = std::cout);

  // Состояние класса. В случае бездействия процессов возвращает true.
  bool PrintStatus(std::ostream& out = std::cout);

  // Задает MPI-группу процессов.
  void SetMpiCommunicator(const MPI_Comm& mpi_comm);

  // Процесс синхронизации раба с остальными процессами.
  void SlaveSynchronize();

  // Обработка сообщений для раба.
  void SlaveRecv(bool* border_gained, bool& quit, bool& notify_master);

  // Содержательная часть игры "Жизнь".
  void CalculatePart();

 private:
  // Распределение поля между процессами.
  void BroadcastField();

  // Обновляет состояние обработки поля.
  bool Running();

 private:
  CyclicVector<CyclicVector<char>> field_;
  CyclicVector<CyclicVector<char>> new_field_;

  size_t iterations_count_;
  size_t desired_iterations_count_;
  bool running_;
  bool up_to_date_;
  std::vector<long long> borders_; // Границы участков поля для процессов.
  CyclicVector<int> process_;  // Номера процессов, обрабатывающих поле.

  MPI_Comm mpi_comm_;
  int world_size_;
  int world_rank_;

  struct Rules {  // Правила игры. Числа окружающих живых ячеек, при которых
    std::vector<char> born_;  // ячейка рождается,
    std::vector<char> stay_;  // ячейка остается жить.
  };
  Rules rules_;
};