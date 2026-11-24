#pragma once

#include <array>
#include <cstddef>

namespace rvn {

/// Fixed-capacity history that overwrites the oldest entry. No allocation after construction,
/// so memory use is bounded no matter how long the app runs. Single-threaded.
template <typename T, std::size_t Capacity> class RingHistory {
public:
    void push(const T& value) noexcept {
        data_[(start_ + size_) % Capacity] = value;
        if (size_ < Capacity) {
            ++size_;
        } else {
            start_ = (start_ + 1) % Capacity;
        }
    }

    void clear() noexcept {
        start_ = 0;
        size_ = 0;
    }

    /// i = 0 is the oldest entry.
    [[nodiscard]] const T& operator[](std::size_t i) const noexcept { return data_[(start_ + i) % Capacity]; }
    [[nodiscard]] const T& back() const noexcept { return (*this)[size_ - 1]; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    /// Copy out oldest-first into any output iterator (e.g. a reused std::vector).
    template <typename OutIt> OutIt copyTo(OutIt out) const {
        for (std::size_t i = 0; i < size_; ++i) {
            *out++ = (*this)[i];
        }
        return out;
    }

private:
    std::array<T, Capacity> data_{};
    std::size_t start_{0};
    std::size_t size_{0};
};

} // namespace rvn
