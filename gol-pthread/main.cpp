#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "multithreading_utils.hpp"
#include "cyclic_vector.hpp"
#include "game_of_life.hpp"

bool StrIsInt(const std::string& str) {
  for (auto c : str) {
    if (c < '0' || c > '9') {
      return false;
    }
  }
  return !str.empty();
}

void PrintHelp() {
  std::cout << "Conway\'s Game of Life.\n"
               "Arguments: <rules> <num_threads>\n"
               "Rules:\n"
               "\tThe rules are set as a first argument of the program in "
               "format (regexp) b\\d+/s\\d+,\n\twhere digits after b are "
               "associated with numbers of alive cells around a cell\n\tneeded "
               "to bring the dead cell alive, and digits after s - to keep the "
               "cell alive.\n\tOriginal rules are b3/s23.\n"
               "Commands:\n"
               "\tstart <n> <m> - create a field sized (n x m) with "
               "number of alive and dead cells\n"
               "\tstart <filename> - create a field from \'filename\' "
               "file (should be .csv format)\n"
               "\tstatus - show current game status\n"
               "\trun <n> - run n iterations of game\n"
               "\tstop - stop calculations if any\n"
               "\tquit - quit program\n"
               "\thelp - show help\n"
               "All commands should be written in lower case!\n";
}

int main(int argc, char** argv) {
  std::string rules = "b3/s23";
  size_t num_threads = 4;
  if (argc >= 2) {
    if (StrIsInt(argv[1])) {
      num_threads = std::stol(argv[2]);
    } else {
      rules = argv[1];
    }
  }
  if (argc >= 3) {
    if (StrIsInt(argv[2])) {
      num_threads = std::stol(argv[2]);
    } else {
      rules = argv[2];
    }
  }
  GameOfLife gol(num_threads, rules);

  for (size_t i = 0; i < 22; ++i) {
    std::cout << "\u2550";
  }
  std::cout << "\n";
  PrintHelp();
  for (size_t i = 0; i < 22; ++i) {
    std::cout << "\u2550";
  }
  std::cout << "\n";

  bool quit = false;
  while (!quit) {
    std::string line;
    std::getline(std::cin, line);
    std::istringstream str_stream(line);

    std::vector<std::string> args;
    for (size_t i = 0; !str_stream.eof(); ++i) {
      std::string arg;
      str_stream >> arg;
      args.push_back(std::move(arg));
    }
    if (args.empty()) {
      continue;
    }

    if (args[0] == "start") {
      if (args.size() < 2) {
        std::cout << args[0] << ": not enough arguments\n";
        continue;
      }

      bool correct;
      if (!StrIsInt(args[1])) {
        correct = gol.Start(args[1]);
      } else {
        if (args.size() < 3) {
          std::cout << args[0] << ": not enough arguments\n";
          continue;
        }
        correct = gol.Start(std::stol(args[1]), std::stol(args[2]));
      }

      if (correct) {
        std::cout << "Successfully created field.\n";
      } else {
        std::cout << "Field already created. Quit program to make a new one.\n";
      }

    } else if (args[0] == "status") {
      if (gol.PrintStatus()) {
        gol.PrintField();
      }

    } else if (args[0] == "run") {
      if (args.size() < 2) {
        std::cout << args[0] << ": not enough arguments\n";
        continue;
      }
      if (!StrIsInt(args[1])) {
        std::cout << args[0] << ": invalid argument.\n";
        continue;
      }
      if (!gol.Run(std::stol(args[1]))) {
        std::cout << args[0]
                  << ": already running or no field has been created yet.\n";
      } else {
        std::cout << "Started running " << std::stol(args[1]) << " iterations.\n";
      }

    } else if (args[0] == "stop") {
      if (gol.Stop()) {
        gol.PrintStatus();
      } else {
        std::cout << args[0]
                  << ": no field has been created yet.\n";
      }

    } else if (args[0] == "quit") {
      gol.Quit();
      if (gol.PrintStatus()) {
        gol.PrintField();
      }
      quit = true;

    } else if (args[0] == "help" || args[0] == "HELP") {
      PrintHelp();
    } else {
      std::cout << args[0] << ": unknown command.\n";
    }
  }

  return 0;
}