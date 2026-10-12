#include "MotionTimeline.h"
#include <mutex>
namespace witchcraft {
namespace {
std::mutex mutex;
MotionTimeline timeline;
MotionStats stats;
}
skycraft::proto::McState RenderMotionState(const skycraft::proto::McState& mc,
    std::int64_t now,std::int64_t frequency,double leadMs) {
    std::lock_guard lock(mutex);
    const auto result=timeline.Render(mc,now,frequency,leadMs);
    if(leadMs==0) {++stats.renderFrames;if(timeline.HeldMoving()) ++stats.heldMovingFrames;}
    stats.bufferMs=timeline.DelayMs();
    return result;
}
MotionStats ReadMotionStats() {
    std::lock_guard lock(mutex);
    const auto result=stats;stats.renderFrames=stats.heldMovingFrames=0;return result;
}
}
