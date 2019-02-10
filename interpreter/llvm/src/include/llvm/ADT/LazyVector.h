//===- llvm/ADT/LazyVector.h - An index map implementation ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a lazy vector.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_LAZYVECTOR_H
#define LLVM_ADT_LAZYVECTOR_H

#include <array>
#include <cassert>
#include <memory>
#include <stddef.h>
#include <vector>

namespace llvm {

template <typename T, std::size_t ChunkSize> class LazyVector {
  typedef std::array<T, ChunkSize> Chunk;
  typedef std::unique_ptr<Chunk> ChunkPtr;
  std::vector<ChunkPtr> Chunks;

  std::size_t RealSize = 0;

  Chunk *getChunk(std::size_t Index) {
    std::size_t ChunkIndex = Index / ChunkSize;

    Chunk *Result = Chunks.at(ChunkIndex).get();
    if (Result == nullptr) {
      ChunkPtr NewChunk(Result = new Chunk());
      Chunks[ChunkIndex] = std::move(NewChunk);
    }
    return Result;
  }

  /// Calculates the amount of chunks necessary for a vector of the given size.
  std::size_t neededChunksForSize(std::size_t Size) {
    // For an empty vector we don't need any chunks.
    if (Size == 0)
      return 0;
    return (Size / ChunkSize) + 1U;
  }

  static_assert(ChunkSize != 0U, "Chunk size can't be zero");

public:
  typedef T value_type;

  T &operator[](std::size_t Index) {
    Chunk *C = getChunk(Index);
    std::size_t Rem = Index % ChunkSize;
    return (*C)[Rem];
  }

  T &at(std::size_t Index) {
    Chunk *C = getChunk(Index);
    std::size_t Rem = Index % ChunkSize;
    return (*C)[Rem];
  }

  std::size_t size() const { return RealSize; }

  std::size_t capacity() const { return RealSize; }

  std::size_t allocatedElements() const {
    std::size_t Result = 0;
    for (const ChunkPtr &C : Chunks) {
      if (C.get())
        ++Result;
    }
    return Result * ChunkSize;
  }

  void resize(std::size_t NewSize) {
    RealSize = NewSize;
    Chunks.resize(neededChunksForSize(NewSize));
  }

  void reserve(std::size_t NewSize) {
    Chunks.reserve(neededChunksForSize(NewSize));
  }

  void clear() {
    RealSize = 0;
    Chunks.clear();
  }

  bool empty() const { return RealSize == 0; }
};

} // end namespace llvm

#endif // LLVM_ADT_LAZYVECTOR_H
