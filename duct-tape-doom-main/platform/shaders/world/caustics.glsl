// Caustics: rippling light from liquids on walls, ceilings and sprites.
// Part of world.frag, which declares the resources these read.
// Caustic pattern rows per animation frame (see buildCaustics): 0 the plain
// liquid flat, 1-4 caustics computed at 8, 24, 48 and 96 units above the
// water, 5-6 the wave slope. Flags: 256 computed layers, 512 shapes grow
// with distance, 2048 slope sway. Growing shapes give each layer its own
// fixed size (2, 2.5, 3 and 4 units per texel; the plain flat 2) and blend
// layers; scaling by the distance itself would shear the pattern, since
// distance varies across a wall far from the map origin.
float causticRow(int layer,int frame,int frames,ivec2 t) {
    return texelFetch(causticPattern,t+ivec2(0,(layer*frames+frame)*64),0).r;
}
float causticLayer(vec2 coord,int layer,float scale,float sway,uint flags) {
    int frames=max(1,textureSize(causticPattern,0).y/(64*7)),frame=int(c.texFilter.z)%frames,next=(frame+1)%frames;
    float blend=c.texFilter.w;
    vec2 q=coord/((flags&FEATURE_CAUSTICS_GROW)!=0u?scale:1.0);
    // The mirror doubles each wave's tilt, so light lands farther off the
    // higher it travels: the pattern sways by the slope, more with distance.
    if((flags&FEATURE_CAUSTICS_SWAY)!=0u) {
        ivec2 t=wrapTexel(ivec2(floor(q)),ivec2(64));
        vec2 slope=mix(vec2(causticRow(5,frame,frames,t),causticRow(6,frame,frames,t)),
            vec2(causticRow(5,next,frames,t),causticRow(6,next,frames,t)),blend);
        q+=(slope*255.0-128.0)/127.0*sway;
    }
    ivec2 t=wrapTexel(ivec2(floor(q)),ivec2(64));
    return mix(causticRow(layer,frame,frames,t),causticRow(layer,next,frames,t),blend);
}
float causticPatternAt(vec2 coord,float distance,uint flags) {
    float sway=saturate(distance/96.0)*6.0;
    if((flags&FEATURE_CAUSTICS_COMPUTED)==0u) return causticLayer(coord,0,2.0,sway,flags);
    float h=clamp(distance,8.0,96.0);
    float l=h<24.0?(h-8.0)/16.0:h<48.0?1.0+(h-24.0)/24.0:2.0+(h-48.0)/48.0;
    int l0=min(int(l),2);float lf=l-float(l0);
    const float scales[4]=float[4](2.0,2.5,3.0,4.0);
    return mix(causticLayer(coord,1+l0,scales[l0],sway,flags),causticLayer(coord,2+l0,scales[l0+1],sway,flags),lf);
}
float causticSteps(float v) {return round(saturate(v*2.0-0.15)*4.0)/4.0;}
// Walls: liquids sit in pits, so the light the water reflects plays on their
// sides. The pattern follows the liquid flat's own animation, on the frame
// its floor shows and crossfading the same way (texFilter.zw); only its
// bright lines light up, over black. With computed layers it is the light
// the flat's waves would actually gather, picked by how far the light has
// traveled from the water: soft near it, sharp lines around 40 units, broader
// above. Walls sample one cell toward their visible side and fade with
// height above their floor. Ceilings (FEATURE_CEILING_CAUSTICS) take the pattern in map space,
// so they mirror the floor below, and fade as their sector gets taller.
// The ripples follow the light on the liquid's sector, not the wall's:
// nothing below mid levels, full in bright rooms. Nukage, slime and lava glow
// by themselves and keep a weak ripple. Dynamic lights reaching this point
// (the flashlight; with FEATURE_CAUSTICS_SHOTS also muzzle flashes and shots; static
// lights only for the share the bake lacks) add their own: a flat surface mirrors the point below the
// water, and where the ray toward that mirror image meets the water over a
// liquid cell, the reflection lands here. It fades over its whole path, down
// to the water and back up, faster than the direct light since the waves
// spread it (the flashlight's long cone; short lights fade over their radius
// anyway). FEATURE_CAUSTICS_ANGLE: water reflects more the flatter the light strikes
// (Schlick's Fresnel), so aiming across a pool lights the walls and aiming
// down barely does. The layer follows each light's distance from the water,
// weighted by brightness. Light that passes through the liquid takes its
// tint; what the surface reflects keeps the light's own color, so the
// brightest lines also catch a little of it.
// Sprites take the same light on their billboard (they face the eye), fading
// with height above the water rather than above a sector floor.
vec3 caustics(vec3 world,vec3 normal,uint sector,bool sprite) {
    uint flags=uint(c.flashlightTint.w);
    bool ceiling=normal.z<-0.5;
    vec2 at=ceiling?world.xy:world.xy+normal.xy/c.map.z;
    ivec2 mapSize=textureSize(causticMap,0);
    vec2 g=(at-c.map.xy)*c.map.z;
    float proximity=textureLod(causticMap,g/vec2(mapSize),0.0).a;
    if(proximity<0.004) return vec3(0);
    ivec2 pick=clamp(ivec2(floor(g)),ivec2(0),mapSize-1);
    // A ceiling reads only cells of its own sector: a cell straddling the
    // edge of a raised lift in a pool, or a void cell behind it (filled in
    // for walls), would put the pool's ripples on the lift's ceiling.
    if(ceiling&&!sprite&&uint(round(texelFetch(seams,pick,0).x*65535.0))!=sector) {
        ivec2 q=ivec2(floor(g-0.5));bool found=false;
        for(int n=0;n<4&&!found;++n) {
            ivec2 o=clamp(q+ivec2(n&1,n>>1),ivec2(0),mapSize-1);
            if(uint(round(texelFetch(seams,o,0).x*65535.0))==sector) {pick=o;found=true;}
        }
        if(!found) return vec3(0);
    }
    uvec4 cell=uvec4(round(texelFetch(causticMap,pick,0)*255.0));
    if(cell.a==0u) return vec3(0);
    vec4 water=sectorInfo[cell.r|((cell.g&127u)<<8)];
    vec3 tint=vec3(float(cell.b>>5)/7.0,float((cell.b>>2)&7u)/7.0,float(cell.b&3u)/3.0);
    float drive=max(smoothstep(0.5,0.9,water.x),(cell.g&128u)!=0u?0.3:0.0),leg=0.0;
    vec3 flash=vec3(0);float weight=0.0;
    uvec2 mask=world.z>water.y?vLightMask:uvec2(0);
    vec3 mirrored=vec3(world.xy,2.0*water.y-world.z);
    while(any(notEqual(mask,uvec2(0)))) {
        uint word=mask.x!=0u?0u:1u;
        uint i=word*32u+uint(findLSB(mask[word]));
        mask[word]&=mask[word]-1u;
        vec3 from=lights[i].position.xyz;
        if(from.z<=water.y||lights[i].baked>=1.0||(lights[i].direction.w<=0.0&&(flags&FEATURE_CAUSTICS_SHOTS)==0u)) continue;
        vec3 surface=mix(from,mirrored,(from.z-water.y)/(from.z-mirrored.z));
        ivec2 below=clamp(ivec2(floor((surface.xy-c.map.xy)*c.map.z)),ivec2(0),mapSize-1);
        if(texelFetch(causticMap,below,0).a<=0.95||dot(normal,surface-world)<=0.0) continue;
        // The flashlight's long cone fades faster than its direct light; short
        // point lights (flashes, shots) already die off over their radius
        // and only last a moment, so they reflect strongly.
        float spread=lights[i].direction.w>0.0?pow(max(0.0,1.0-length(mirrored-from)/lights[i].position.w),2.0)*1.5:2.5;
        float amount=flashAtOpen(mirrored,i)*spread*(1.0-lights[i].baked);
        if(amount<=0.0||flashAt(surface,i)<=0.0) continue;
        if((flags&FEATURE_CAUSTICS_ANGLE)!=0u) {
            float cosine=(from.z-water.y)/max(length(surface-from),0.001);
            amount*=min((0.02+0.98*pow(1.0-cosine,5.0))/0.14,2.5);
        }
        flash+=lights[i].color.rgb*amount;
        leg+=length(world-surface)*amount;weight+=amount;
    }
    leg=weight>0.0?leg/weight:0.0;
    bool flashing=max(flash.r,max(flash.g,flash.b))>=0.004;
    if(drive<0.004&&!flashing) return vec3(0);
    vec4 info=sectorInfo[sector];
    float fade=ceiling?1.0-smoothstep(32.0,224.0,info.z-info.y):1.0-smoothstep(0.0,96.0,world.z-(sprite?water.y:info.y));
    vec2 coord=ceiling?world.xy:vec2(dot(world.xy,vec2(-normal.y,normal.x)),water.y-world.z);
    float room=drive>=0.004?causticSteps(causticPatternAt(coord,max(world.z-water.y,0.0),flags)):0.0;
    float lit=flashing?causticSteps(causticPatternAt(coord,leg,flags)):0.0;
    vec3 glint=lit>=1.0?min(flash,vec3(3.0))*0.35:vec3(0);
    return (tint*(room*min(drive,3.0)+lit*min(flash,vec3(3.0)))+glint)*proximity*fade*0.45;
}
