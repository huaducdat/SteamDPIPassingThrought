#pragma once

#include "packet/PacketParser.h"

#include <memory>
#include <string>
#include <vector>

namespace steamdpi::packet {

struct TransformationResult final {
  bool success{};
  bool modified{};
  PacketValidation validation;
  std::string error;
  // Present only when a transform explicitly creates a modified copy.
  std::unique_ptr<PacketBuffer> modifiedCopy;
};

class PacketTransform {
 public:
  virtual ~PacketTransform() = default;
  [[nodiscard]] virtual TransformationResult apply(const PacketBuffer& packet) const = 0;
};

class NoOpTransform final : public PacketTransform {
 public:
  [[nodiscard]] TransformationResult apply(const PacketBuffer& packet) const override;
};
class ValidationTransform final : public PacketTransform {
 public:
  [[nodiscard]] TransformationResult apply(const PacketBuffer& packet) const override;
};
class DryRunTransform final : public PacketTransform {
 public:
  [[nodiscard]] TransformationResult apply(const PacketBuffer& packet) const override;
};

class PacketTransformPipeline final {
 public:
  static constexpr std::size_t kMaxTransforms = 16;
  bool add(std::unique_ptr<PacketTransform> transform);
  [[nodiscard]] TransformationResult run(const PacketBuffer& packet) const;
  [[nodiscard]] std::size_t size() const noexcept { return transforms_.size(); }
  void clear() noexcept { transforms_.clear(); }

 private:
  std::vector<std::unique_ptr<PacketTransform>> transforms_;
};

}
