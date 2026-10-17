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
    {
        // The span compositor matches scaling the HUD and blending every row.
        const int w=13,h=7,tw=31,th=19;
        std::vector<std::uint8_t> hud(static_cast<std::size_t>(w)*h*4,0);
        for(int i=0;i<w*h;++i) if(i%5==0||i%7==3) {hud[i*4]=static_cast<std::uint8_t>(i*3);hud[i*4+1]=40;hud[i*4+2]=200;hud[i*4+3]=static_cast<std::uint8_t>(100+i%150);}
        for(bool bottom:{false,true}) {
            std::vector<std::uint8_t> a(static_cast<std::size_t>(tw)*th*4),b;
            for(std::size_t i=0;i<a.size();++i) a[i]=static_cast<std::uint8_t>(i*7);
            b=a;
            std::vector<std::uint8_t> scaled;
            if(!witchcraft::ScaleHud(hud,w,h,bottom,tw,th,scaled)) return 80;
            for(int y=0;y<th;++y) for(int x=0;x<tw;++x) {
                const auto* p=scaled.data()+(y*tw+x)*4;auto* d=a.data()+(y*tw+x)*4;if(!p[3]) continue;const int inv=255-p[3];
                d[0]=static_cast<std::uint8_t>(p[2]+d[0]*inv/255);d[1]=static_cast<std::uint8_t>(p[1]+d[1]*inv/255);
                d[2]=static_cast<std::uint8_t>(p[0]+d[2]*inv/255);d[3]=static_cast<std::uint8_t>(p[3]+d[3]*inv/255);
            }
            std::vector<int> lo,hi;if(!witchcraft::HudRowSpans(hud,w,h,lo,hi)) return 81;
            witchcraft::BlendScaledHud(b.data(),tw,th,hud,w,h,bottom,lo,hi);
            if(a!=b) return 82;
        }
    }}
