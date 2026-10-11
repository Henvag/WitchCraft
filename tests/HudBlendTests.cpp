#include "HudBlend.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
int main() {
    // All alpha values, all destination channel values, mixed pixels and
    // unaligned pointers/tails. Compare against premultiplied source-over.
    for(unsigned offset=0;offset<4;++offset) for(unsigned count:{1u,3u,4u,7u,17u}) {
        std::vector<unsigned char> source(count*4+16),actual(count*4+16),expected(actual.size());
        for(unsigned a=0;a<256;++a) for(unsigned d=0;d<256;++d) {
            for(unsigned x=0;x<count;++x) {
                auto* p=source.data()+offset+x*4;auto* q=actual.data()+offset+x*4;
                const unsigned alpha=(x%4==0)?0:(x%4==1)?255:a;
                p[0]=alpha/3;p[1]=alpha/2;p[2]=alpha;p[3]=alpha;
                q[0]=d;q[1]=255-d;q[2]=d/2;q[3]=255;
            }
            expected=actual;
            for(unsigned x=0;x<count;++x) {
                auto* p=source.data()+offset+x*4;auto* q=expected.data()+offset+x*4;
                const unsigned inv=255-p[3];
                q[0]=p[2]+q[0]*inv/255;q[1]=p[1]+q[1]*inv/255;
                q[2]=p[0]+q[2]*inv/255;q[3]=p[3]+q[3]*inv/255;
            }
            witchcraft::BlendHudRow(actual.data()+offset,source.data()+offset,count);
            if(actual!=expected) {std::printf("HUD mismatch offset=%u count=%u alpha=%u destination=%u\n",offset,count,a,d);return 1;}
        }
    }
}
