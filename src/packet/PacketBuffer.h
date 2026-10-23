#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace steamdpi::packet {

inline constexpr std::size_t kMaxPacketSize = 65575;

class MutablePacketCopy final {
 public:
  explicit MutablePacketCopy(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}
  [[nodiscard]] std::span<std::uint8_t> bytes() noexcept { return bytes_; }
  [[nodiscard]] std::vector<std::uint8_t> freezeBytes() const { return bytes_; }
 private:
  std::vector<std::uint8_t> bytes_;
};

class PacketBuffer final {
 public:
  PacketBuffer() = default;
  explicit PacketBuffer(std::span<const std::uint8_t> bytes);
  explicit PacketBuffer(std::vector<std::uint8_t> bytes);

  [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }
  [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
  [[nodiscard]] bool empty() const noexcept { return bytes_.empty(); }
  [[nodiscard]] bool exceedsLimit() const noexcept { return exceedsLimit_; }
  [[nodiscard]] MutablePacketCopy mutableCopy() const { return MutablePacketCopy(bytes_); }

 private:
  std::vector<std::uint8_t> bytes_;
  bool exceedsLimit_{};
};

}
