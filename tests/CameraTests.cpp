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
}
