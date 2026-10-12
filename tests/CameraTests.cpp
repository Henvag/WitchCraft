#include "NativeCamera.h"
#include <cstdlib>
using namespace witchcraft::native;
using namespace skycraft::proto;
int main() {
    McState pose{};pose.x=100;pose.y=12;pose.z=-50;pose.eyeHeight=1.62f;pose.cameraMode=1;
    float ground[]{100,50,10},original[]{98,48,13},out[3]{};
    if(!CameraPosition(pose,ground,original,out)||out[2]!=13) return 1;
    pose.flags=kMcCanFly|kMcFlying;
    if(!CameraPosition(pose,ground,original,out)||out[0]!=98||out[1]!=48||out[2]!=15) return 2;
    pose.cameraMode=0;
    if(!CameraPosition(pose,ground,original,out)||out[0]!=100||out[1]!=50||std::abs(out[2]-13.62f)>.0001) return 3;
    pose.eyeHeight=.4f; // Sneaking/swimming eye height remains Minecraft-owned.
    if(!CameraPosition(pose,ground,original,out)||std::abs(out[2]-12.4f)>.0001) return 4;
    pose.y=267;if(CameraPosition(pose,ground,original,out)) return 5;
    pose.y=std::numeric_limits<double>::quiet_NaN();if(CameraPosition(pose,ground,original,out)) return 6;
}
