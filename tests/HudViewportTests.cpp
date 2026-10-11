#include "HudViewport.h"
#include "HudScale.h"
#include <cstdlib>
using namespace witchcraft;
void Check(bool ok) {if(!ok) std::abort();}
int main() {
    const auto hd=TransferViewport(2560,1440);Check(hd.width==1280&&hd.height==720);
    const auto wide=TransferViewport(3440,1440);Check(wide.width==1280&&wide.height==535);
    const auto small=TransferViewport(800,600);Check(small.width==800&&small.height==600);
    const auto full=TransferViewport(2560,1440,true);Check(full.width==2560&&full.height==1440);
    Check(TransferViewport(0,1440).width==0);
    Check(HudCoordinate(1280,2560,1280)==640&&HudCoordinate(720,1440,720)==360);
    Check(HudCoordinate(-9,2560,1280)==0&&HudCoordinate(3000,2560,1280)==1279);
    Check(HudCoordinate(2560,0,1280)==0);
    // Every native pixel maps to the same source pixel used by nearest-neighbour HUD composition.
    for(int width:{800,1920,2560,3440}) {
        const auto size=TransferViewport(width,1440);
        for(int x=0;x<width;++x) Check(HudCoordinate(x,width,size.width)==x*size.width/width);
    }
    std::vector<std::uint8_t> source(3*2*4),scaled;
    for(unsigned i=0;i<source.size();++i) source[i]=static_cast<std::uint8_t>(i);
    for(bool flip:{false,true}) for(int w:{2,3,6,11}) for(int h:{1,2,4,7}) {
        Check(ScaleHud(source,3,2,flip,w,h,scaled));
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) for(int c=0;c<4;++c) {
            int sy=HudCoordinate(y,h,2);if(flip) sy=1-sy;
            Check(scaled[(y*w+x)*4+c]==source[(sy*3+HudCoordinate(x,w,3))*4+c]);
        }
    }
    Check(!ScaleHud({},3,2,false,6,4,scaled)&&scaled.empty());
}
