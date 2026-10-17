#include "NativeCamera.h"
#include <cstdlib>
using namespace witchcraft::native;
using namespace skycraft::proto;
int main() {
    McState pose{};pose.x=100;pose.y=12;pose.z=-50;pose.eyeHeight=1.62f;pose.cameraMode=1;
    float ground[]{100,50,10},original[]{98,48,13},out[3]{};
    if(!CameraPosition(pose,ground,original,out)||out[2]!=15) return 1;
    pose.flags=kMcCanFly|kMcFlying;
    if(!CameraPosition(pose,ground,original,out)||out[0]!=98||out[1]!=48||out[2]!=15) return 2;
    pose.x=102;pose.z=-51;pose.flags=0;
    if(!CameraPosition(pose,ground,original,out)||out[0]!=100||out[1]!=49||out[2]!=15) return 7;
    pose.x=100;pose.z=-50;
    pose.cameraMode=0;
    if(!CameraPosition(pose,ground,original,out)||out[0]!=100||out[1]!=50||std::abs(out[2]-13.62f)>.0001) return 3;
    pose.eyeHeight=.4f; // Sneaking/swimming eye height remains Minecraft-owned.
    if(!CameraPosition(pose,ground,original,out)||std::abs(out[2]-12.4f)>.0001) return 4;
    pose.y=267;if(CameraPosition(pose,ground,original,out)) return 5;
    pose.y=std::numeric_limits<double>::quiet_NaN();if(CameraPosition(pose,ground,original,out)) return 6;
    // First-person position never inherits native ground animation displacement.
    pose.y=12;pose.eyeHeight=1.62f;
    float fallenAnchor[]{101.5f,48.2f,7.5f},fallenCamera[]{99,46,10};
    if(!CameraPosition(pose,fallenAnchor,fallenCamera,out)||out[0]!=100||out[1]!=50||
        std::abs(out[2]-13.62f)>.0001) return 8;
    // Native anchor translation cancels out of third-person framing when both
    // the native camera and its anchor move together.
    pose.cameraMode=1;
    float translatedAnchor[]{101,52,7},translatedCamera[]{99,50,10};
    float baseline[3]{};
    if(!CameraPosition(pose,ground,original,baseline)||
        !CameraPosition(pose,translatedAnchor,translatedCamera,out)) return 9;
    for(int i=0;i<3;++i) if(std::abs(out[i]-baseline[i])>.0001) return 10;
    pose.cameraMode=3;if(CameraPosition(pose,ground,original,out)) return 11;
    pose.cameraMode=0;pose.eyeHeight=std::numeric_limits<float>::quiet_NaN();
    if(CameraPosition(pose,ground,original,out)) return 12;

    // Front view: learn an unknown rotation layout from built forward vectors.
    const AngleLayout truth{2,0,-1,1,90}; // e.g. (yaw, roll, pitch) with mirrored pitch
    AngleLayoutProbe probe;
    for(unsigned frame=0;frame<200&&!probe.Ready();++frame) {
        const float angles[]{static_cast<float>(frame)*1.7f-40,0,-12.f+static_cast<float>(frame%7)};
        float forward[3]{};AngleForward(angles,truth,forward);
        probe.Observe(angles,forward);
    }
    if(!probe.Ready()) return 13;
    const auto& learned=probe.Locked();
    // Equivalent layouts are fine; the forward they produce must match.
    for(float yaw:{-170.f,-30.f,15.f,95.f}) {
        const float angles[]{yaw,0,-20};float a[3]{},b[3]{};
        AngleForward(angles,truth,a);AngleForward(angles,learned,b);
        if(a[0]*b[0]+a[1]*b[1]+a[2]*b[2]<.9999f) return 14;
    }
    // Level views and ambiguous inputs never lock a layout.
    AngleLayoutProbe level;
    for(unsigned frame=0;frame<200;++frame) {
        const float angles[]{static_cast<float>(frame),0,0};float forward[3]{};
        AngleForward(angles,truth,forward);level.Observe(angles,forward);
    }
    if(level.Ready()) return 15;
    // A steady view without heading change never locks either.
    AngleLayoutProbe steady;
    for(unsigned frame=0;frame<200;++frame) {
        const float angles[]{30,0,-15};float forward[3]{};
        AngleForward(angles,truth,forward);steady.Observe(angles,forward);
    }
    if(steady.Ready()) return 16;
    // The front camera sits along the look direction and faces back at the eye.
    McState front{};front.x=10;front.y=60;front.z=-20;front.eyeHeight=1.62f;front.cameraMode=2;front.cameraDistance=3;
    const float angles[]{40,0,-15};float position[3]{},turned[3]{},look[3]{},view[3]{};
    if(!FrontCamera(front,angles,learned,position,turned,look)) return 17;
    const float eye[]{10,20,61.62f};
    for(int i=0;i<3;++i) if(std::abs(position[i]-(eye[i]+look[i]*3))>.0001f) return 18;
    if(!AngleForward(turned,learned,view)) return 19;
    for(int i=0;i<3;++i) if(std::abs(view[i]+look[i])>.0001f) return 20;
    front.cameraDistance=0; // Unknown zoom falls back to vanilla's 4 blocks.
    if(!FrontCamera(front,angles,learned,position,turned,look)||std::abs(position[0]-(eye[0]+look[0]*4))>.0001f) return 21;
    front.y=std::numeric_limits<double>::quiet_NaN();
    if(FrontCamera(front,angles,learned,position,turned,look)) return 22;
    if(FrontCamera(pose,angles,AngleLayout{},position,turned,look)) return 23;
    // Minecraft-style look: right turns clockwise, mouse down looks down, and
    // pitch reaches straight up/down instead of Witcher's clamp.
    const AngleLayout live{1,2,1,1,0}; // Verified in game: (roll, pitch, yaw).
    float turn[3]{0,-10,30},before[3]{},after[3]{};
    AngleForward(turn,live,before);
    if(!ApplyLook(turn,live,100,0,.5f)) return 24;
    AngleForward(turn,live,after);
    if(before[0]*after[1]-before[1]*after[0]>=0) return 25; // Clockwise seen from above.
    ApplyLook(turn,live,0,100000,.5f);if(std::abs(turn[1]+89.5f)>1e-3f) return 26;
    ApplyLook(turn,live,0,-200000,.5f);if(std::abs(turn[1]-89.5f)>1e-3f) return 27;
    if(std::abs(LookDegreesPerCount(.5f)-.5f*.5f*.5f*1.2f)>1e-6f) return 28;
    // The behind view sits Minecraft's camera distance back along the look.
    front.y=60;front.cameraMode=1;front.cameraDistance=4;
    if(!DetachedCamera(front,angles,learned,false,position,turned,look)) return 29;
    for(int i=0;i<3;++i) if(std::abs(position[i]-(eye[i]-look[i]*4))>.0001f||turned[i]!=angles[i]) return 30;
}
