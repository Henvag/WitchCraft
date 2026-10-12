#include "OcclusionMesh.h"
#include <limits>
using namespace witchcraft;
using namespace skycraft::proto;
int main() {
    ColTri ground{{1000,10,-2000,1001,10,-2000,1000,10,-1999},0};
    ColTri wall{{1000,10,-2000,1000,12,-2000,1001,10,-2000},0};
    ColTri bad=ground;bad.v[0]=std::numeric_limits<float>::quiet_NaN();
    ColTri degenerate{};double origin[3]{};
    const auto vertices=OcclusionVertices({bad,ground,degenerate,wall},origin);
    if(vertices.size()!=6||origin[0]!=1000||origin[1]!=10||origin[2]!=-2000) return 1;
    if(vertices[1].x!=1||vertices[2].z!=1||vertices[4].y!=2) return 2;
    for(const auto& vertex:vertices) if(!std::isfinite(vertex.x)||!std::isfinite(vertex.y)||!std::isfinite(vertex.z)) return 3;
    if(!OcclusionVertices({bad,degenerate},origin).empty()) return 4;
}
