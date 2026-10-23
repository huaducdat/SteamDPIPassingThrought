#pragma once

#include "packet/PacketBuffer.h"
#include "packet/PacketMetadata.h"

namespace steamdpi::packet {

struct PacketParseOptions final {
  bool verifyIpv4HeaderChecksum{true};
  bool verifyTransportChecksum{true};
};

class PacketParser final {
 public:
  [[nodiscard]] PacketValidation parse(const PacketBuffer& packet,
                                       PacketParseOptions options = {}) const;
};

class PacketValidator final {
 public:
  [[nodiscard]] PacketValidation validate(const PacketBuffer& packet) const;
};

}
