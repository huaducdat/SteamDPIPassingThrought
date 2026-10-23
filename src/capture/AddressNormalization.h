#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace steamdpi {

// Convert WinDivert's host-order 32-bit address words to network-order bytes.
inline std::array<std::uint8_t,16> canonicalWinDivertAddress(
    const std::array<std::uint32_t,4>& words) noexcept {
  std::array<std::uint8_t,16> bytes{};
  for(std::size_t i=0;i<words.size();++i) {
    bytes[i*4]=static_cast<std::uint8_t>(words[i]>>24);
    bytes[i*4+1]=static_cast<std::uint8_t>(words[i]>>16);
    bytes[i*4+2]=static_cast<std::uint8_t>(words[i]>>8);
    bytes[i*4+3]=static_cast<std::uint8_t>(words[i]);
  }
  return bytes;
}

} // namespace steamdpi
