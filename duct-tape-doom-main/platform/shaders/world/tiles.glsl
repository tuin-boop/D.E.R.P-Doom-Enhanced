// Uneven tiles (FEATURE_UNEVEN_TILES): every tile, brick or plate painted into
// a floor, ceiling or wall lies a little crooked, as if laid by hand, so a
// flat surface stops repeating perfectly. The relief's alpha names each
// texel's tile (flatTiles and wallTiles in platform/painted_tiles.h); a hash
// of the tile's anchor picks its lean and height, so each repeat differs.
// Flats take world coordinates, so the anchor alone places a tile; wall
// coordinates restart on every line, so walls add their section's seed
// (vStatic). Where two tiles meet, the texels on both sides ramp toward the
// other tile's height: one side of the seam catches the light as a thin lip,
// the other falls into shadow.
// Part of world.frag, which declares the resources these read.
const float tileTilt=0.06; // Largest lean, as a relief slope (about 3.5 degrees).
const float tileStep=1.0;  // Largest rise or drop from the surface, in map units.
const float tileLip=0.35;  // Lip slope per map unit of height between neighbors.
uint tileHash(ivec2 p,uint seed) {
    uint h=uint(p.x)*0x8da6b343u^uint(p.y)*0xd8163841u^seed*0xcb1ab31fu;
    // A two-round mixer from Chris Wellons' hash-prospector (public domain).
    h^=h>>15;h*=0x2c1b3c6du;h^=h>>12;h*=0x297a2d39u;h^=h>>15;
    return h;
}
// The anchor texel (texture space, unwrapped) of the tile under texel p. A
// wall row without joints is one long tile: its offset along the row is
// noTileAxis (127), so the anchor stays 0 there and free marks that axis.
bool tileAnchor(ivec2 p,ivec2 size,out ivec2 anchor,out bvec2 free) {
    uint packed=uint(round(texelFetch(relief,wrapTexel(p,size),0).a*65535.0));
    ivec2 offset=ivec2(int(packed>>8u),int(packed&255u))-128;
    free=equal(offset,ivec2(127));
    anchor=ivec2(free.x?0:p.x+offset.x,free.y?0:p.y+offset.y);
    return packed!=0xFFFFu;
}
// The tile's lean (xy, slope along u and v) and height at its anchor (z); a
// long tile leans only across itself.
vec3 tilePose(ivec2 anchor,bvec2 free,uint seed) {
    uint h=tileHash(anchor,seed);
    vec3 r=vec3(h&1023u,(h>>10u)&1023u,(h>>20u)&1023u)/1023.0*2.0-1.0;
    return vec3(mix(r.xy*tileTilt,vec2(0),free),r.z*tileStep);
}
// Its height at a point in texture space.
float tileHeight(ivec2 anchor,vec3 pose,vec2 at) {return pose.z+dot(pose.xy,at-vec2(anchor)-0.5);}
// Slope to add to the relief at texel p: the tile's lean, plus toward each
// neighboring tile a ramp to its height at the shared edge.
vec2 tileSlope(ivec2 p,ivec2 size,uint seed) {
    ivec2 own;bvec2 free;if(!tileAnchor(p,size,own,free)) return vec2(0);
    vec3 pose=tilePose(own,free,seed);vec2 slope=pose.xy,lip=vec2(0);
    const ivec2 steps[4]=ivec2[4](ivec2(1,0),ivec2(-1,0),ivec2(0,1),ivec2(0,-1));
    for(int i=0;i<4;++i) {
        ivec2 other;bvec2 otherFree;
        if(!tileAnchor(p+steps[i],size,other,otherFree)||(other==own&&otherFree==free)) continue;
        vec2 edge=vec2(p)+0.5+0.5*vec2(steps[i]);
        lip+=vec2(steps[i])*(tileHeight(other,tilePose(other,otherFree,seed),edge)-tileHeight(own,pose,edge))*tileLip;
    }
    return slope+clamp(lip,-0.6,0.6);
}
