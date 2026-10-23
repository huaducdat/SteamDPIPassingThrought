#include "packet/PacketTransformPipeline.h"

namespace steamdpi::packet {

TransformationResult NoOpTransform::apply(const PacketBuffer& packet) const {
  TransformationResult r; r.success = true; r.validation = PacketParser{}.parse(packet); return r;
}
TransformationResult ValidationTransform::apply(const PacketBuffer& packet) const {
  TransformationResult r; r.validation = PacketValidator{}.validate(packet); r.success = r.validation.valid;
  if (!r.success) r.error = r.validation.detail; return r;
}
TransformationResult DryRunTransform::apply(const PacketBuffer& packet) const {
  TransformationResult r; r.validation = PacketParser{}.parse(packet); r.success = r.validation.valid;
  if (!r.success) r.error = r.validation.detail; return r;
}
bool PacketTransformPipeline::add(std::unique_ptr<PacketTransform> transform) {
  if (!transform || transforms_.size() >= kMaxTransforms) return false;
  transforms_.push_back(std::move(transform)); return true;
}
TransformationResult PacketTransformPipeline::run(const PacketBuffer& packet) const {
  TransformationResult result; result.success = true;
  for (const auto& transform : transforms_) {
    result = transform->apply(packet);
    if (!result.success) return result;
    if (result.modified || result.modifiedCopy) {
      result.success = false; result.modified = false; result.modifiedCopy.reset();
      result.error = "offline V1 pipeline rejects packet mutation"; return result;
    }
  }
  return result;
}

}
