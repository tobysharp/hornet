#pragma once

#include <cstdint>
#include <iterator>
#include <vector>

namespace hornet::data::utxo {

template <typename T>
class TiledVector {
 public:
  using Tile = std::vector<T>;
  template <typename T> class IteratorT;
  using Iterator = IteratorT<TiledVector>;
  using ConstIterator = IteratorT<const TiledVector>;

  TiledVector(int entry_bits = 13) :
   entry_bits_(entry_bits), entries_per_tile_(1 << entry_bits), entry_mask_(entries_per_tile_ - 1) {
  }
  TiledVector(TiledVector&& rhs) : tiles_(std::move(rhs.tiles_)), entry_bits_(rhs.entry_bits_), entries_per_tile_(rhs.entries_per_tile_), entry_mask_(rhs.entry_mask_) {}
  
  // Returns the number of entries across all tiles.
  size_t Size() const { 
    return tiles_.empty() ? 0 : (tiles_.size() - 1) * entries_per_tile_ + tiles_.back().size(); 
  }

  bool Empty() const {
    return Size() == 0;
  }

  void Clear() {
    tiles_.clear();
  }

  // Append an entry to the last tile, starting a new tile if necessary.
  template <typename... Args>
  void EmplaceBack(Args&&... args) {
    if (tiles_.empty() || tiles_.back().size() >= entries_per_tile_) {
      tiles_.emplace_back();
      tiles_.back().reserve(entries_per_tile_);
    }
    tiles_.back().emplace_back(std::forward<Args>(args)...);
  }

  void PushBack(const T& value) {
    if (tiles_.empty() || tiles_.back().size() >= entries_per_tile_) {
      tiles_.emplace_back();
      tiles_.back().reserve(entries_per_tile_);
    }
    tiles_.back().push_back(value);
  }

  template <typename Pred>
  size_t EraseIf(Pred&& pred) {
    size_t removed = 0;
    for (auto& tile : tiles_)
      removed += std::erase_if(tile, std::forward<Pred>(pred));
    std::erase_if(tiles_, [](const Tile& t) { return t.empty(); });
    return removed;
  }

  const T& operator[](size_t index) const {
    const size_t tile_index = index >> entry_bits_;
    const size_t entry_index = index & entry_mask_;
    return tiles_[tile_index][entry_index];
  }

  bool IsContiguous(size_t begin, size_t end) const {
    const ssize_t size = end - begin;
    const size_t tile_index = begin >> entry_bits_;
    const size_t entry_index = begin & entry_mask_;
    return entry_index + size <= tiles_[tile_index].size();
  }

  std::span<const T> Span(size_t begin, size_t end) const {
    const ssize_t size = end - begin;
    const size_t tile_index = begin >> entry_bits_;
    const Tile& tile = tiles_[tile_index];
    const size_t entry_index = begin & entry_mask_;
    if (entry_index + size > tile.size())
      util::ThrowInvalidArgument("TiledVector::Span requested a non-contiguous range.");
    return {&tile[entry_index], size};
  }

  Iterator begin() { return {*this, 0}; }
  Iterator end() { return {*this, Size()}; }
  ConstIterator begin() const { return {*this, 0}; }
  ConstIterator end() const { return {*this, Size()}; }
  ConstIterator cbegin() const { return {*this, 0}; }
  ConstIterator cend() const { return {*this, Size()}; }

 protected:
  friend class IteratorT;
  std::vector<Tile> tiles_;
  const int entry_bits_;
  const int entries_per_tile_;
  const int entry_mask_;
};

template <typename T>
template <typename Grid>
class TiledVector<T>::IteratorT {
 public:
  using iterator_category = std::random_access_iterator_tag;
  using value_type        = typename Grid::Tile::value_type;
  using difference_type   = std::ptrdiff_t;
  using pointer           = const value_type*;
  using reference         = const value_type&;

  IteratorT(Grid& grid, size_t index) : grid_(grid), index_(index) {}

  bool operator <(const IteratorT& rhs) const {
    return index_ < rhs.index_;
  }
  bool operator <=(const IteratorT& rhs) const {
    return index_ <= rhs.index_;
  }
  bool operator >(const IteratorT& rhs) const {
    return index_ > rhs.index_;
  }
  bool operator >=(const IteratorT& rhs) const {
    return index_ >= rhs.index_;
  }
  bool operator ==(const IteratorT& rhs) const {
    return index_ == rhs.index_;
  }
  bool operator !=(const IteratorT& rhs) const {
    return !operator ==(rhs);
  }
  reference operator *() const {
    return grid_[index_];
  }
  pointer operator ->() const {
    return &grid_[index_];
  }
  IteratorT& operator ++() {
    ++index_;
    return *this;
  }
  IteratorT operator +(difference_type x) const {
    return { grid_, index_ + x };
  }
  IteratorT operator -(difference_type x) const {
    return { grid_, index_ - x };
  }
  IteratorT& operator +=(difference_type x) {
    index_ += x;
    return *this;
  }
  IteratorT& operator -=(difference_type x) {
    index_ -= x;
    return *this;
  }

 private:
  Grid& grid_;
  size_t index_;
};

}  // namespace hornet::data::utxo
