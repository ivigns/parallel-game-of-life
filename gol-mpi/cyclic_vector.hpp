#pragma once

#include <vector>

// Вектор с цикличной индексацией.
template <class T>
class CyclicVector : public std::vector<T> {
 public:
  T& operator[](const long long i) {
    long long size = this->size();
    if (i < 0) {
      return (*this)[size + i % size];
    }
    return this->at(i % size);
  }

  const T& operator[](const long long i) const {
    long long size = this->size();
    if (i < 0) {
      return (*this)[size + i % size];
    }
    return this->at(i % size);
  }
};