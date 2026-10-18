// Dynamic lights and fog. Expects the Camera block `c`, the Lights block and
// the Fog block; define BLOCKERS when the blocker storage buffer is bound.
float cross2(vec2 a,vec2 b) {return a.x*b.y-a.y*b.x;}
float lightFalloff(vec3 point,uint i) {
    vec3 delta=point-lights[i].position.xyz;
    float falloff=max(0.0,1.0-length(delta)/lights[i].position.w);
    if(falloff<=0.0) return 0.0;
    if(lights[i].direction.w>0.0) {
        float cosine=length(delta)>0.001?dot(normalize(delta),lights[i].direction.xyz):1.0;
        float cone=smoothstep(lights[i].direction.w,0.985,cosine);
        falloff*=cone;
    }
    return falloff;
}
// Glowing wall panels (direction.yz: their wall's normal, no cone) shine only
// ahead of the wall they sit 4 units in front of, fading out toward its plane;
// a receiver's normal keeps the wall around the panel lit. Matches
// doom_panel_emission; normal is vec3(0) where there is none.
float panelEmission(vec3 point,vec3 normal,uint i) {
    vec2 n=lights[i].direction.yz;
    if(lights[i].direction.w>0.0||(n.x==0.0&&n.y==0.0)) return 1.0;
    vec3 delta=point-lights[i].position.xyz;
    float ahead=dot(delta.xy,n)+4.0;
    float emission=smoothstep(0.0,0.5,ahead/max(length(delta),1.0));
    return max(emission,max(0.0,dot(normal.xy,n))*max(0.0,1.0-abs(ahead)/8.0));
}
float flashAtOpen(vec3 point,vec3 normal,uint i) {
    float falloff=lightFalloff(point,i);
    if(falloff<=0.0) return 0.0;
    return lights[i].strength*falloff*falloff*(3.0-2.0*falloff)*panelEmission(point,normal,i);
}
float flashAtOpen(vec3 point,uint i) {return flashAtOpen(point,vec3(0),i);}
#ifdef BLOCKERS
// Solid walls and closed portal spans stop the ray; matches doom_flash_at.
// The intersection tests scale by det instead of dividing per blocker.
// A glowing panel (direction.x without a cone) is a vertical span of that
// half-height: the heights s on it whose rays pass every opening narrow to
// [low,high], so a lip hiding part of the panel lets part of its light by.
// One blocker, at word j: false when it stops the ray.
bool flashPasses(uint j,vec3 origin3,vec3 delta,inout float low,inout float high) {
    vec4 line=uintBitsToFloat(lightWords[j]);
    vec2 edge=line.zw-line.xy,origin=line.xy-origin3.xy;
    float det=cross2(delta.xy,edge);
    if(abs(det)<0.0001) return true;
    float t=cross2(origin,edge),u=cross2(origin,delta.xy);
    if(det<0.0) {det=-det;t=-t;u=-u;}
    if(t>0.001*det&&t<0.999*det&&u>=0.0&&u<=det) {
        // The ray from height s on the span crosses at hit+s*lever.
        float along=t/det,hit=origin3.z+delta.z*along,lever=1.0-along;
        vec2 opening=uintBitsToFloat(lightWords[j+1u]).xy+vec2(0.02,-0.02);
        if(opening.x>=opening.y) return false;
        float below=(opening.x-hit)/lever,above=(opening.y-hit)/lever;
        if(below>=high||above<=low) return false;
        low=max(low,below);high=min(high,above);
    }
    return true;
}
// Lights with many blockers (first's top bit) list them per azimuth slice
// after the blockers (doom_flash_slice_blockers): a table of first<<16|count
// per slice, then 16-bit indices. A ray meets no wall outside its azimuth.
#define FLASH_SLICES 256u
// FEATURE_STIPPLED_SHADOWS: the flashlight's lamp is a short diagonal
// segment across the ray, FLASH_LAMP_RADIUS each way, instead of a point.
// Each pixel still casts one ray, from the point on it its ordered-dither
// threshold picks (flashStipple, in -1..1), so a shadow stays hard and only
// its edge breaks into a stipple: lit, sparse dots, checkerboard, dense
// dots, shadow. The
// stipple is 1 Doom pixel where a blocker meets its shadow and widens with
// the distance behind it. Diagonal, so the steps run in order across both
// the vertical edges of wall ends and the level ones of lintels and steps.
#define FLASH_LAMP_RADIUS 1.5
float flashAt(vec3 point,vec3 normal,uint i,float stipple) {
    float amount=flashAtOpen(point,normal,i);
    if(amount<=0.0) return 0.0;
    vec3 origin3=lights[i].position.xyz;
    vec3 delta=point-origin3;
    // Blockers are listed by the slice of the lamp's own ray (below); the
    // segment is short enough that a ray from its end meets the same ones.
    vec3 sliceDelta=delta;
    if(stipple!=0.0&&lights[i].direction.w>0.0&&dot(delta.xy,delta.xy)>1.0) {
        vec2 across=normalize(vec2(-delta.y,delta.x));
        origin3+=vec3(across,1.0)*(stipple*FLASH_LAMP_RADIUS);
        delta=point-origin3;
    }
    float extent=lights[i].direction.w>0.0?0.0:lights[i].direction.x,low=-extent,high=extent;
    uint first=lights[i].first&0x7fffffffu,count=lights[i].count;
    if((lights[i].first&0x80000000u)!=0u) {
        // Straight above or below the light no wall is crossed.
        if(sliceDelta.x!=0.0||sliceDelta.y!=0.0) {
            float turn=(atan(sliceDelta.y,sliceDelta.x)+3.14159265)*(float(FLASH_SLICES)/6.2831853);
            uint slice=min(uint(max(turn,0.0)),FLASH_SLICES-1u);
            uint table=first+2u*count,indices=table+FLASH_SLICES/4u;
            uint entry=lightWords[table+slice/4u][slice%4u];
            for(uint n=entry>>16u,end=n+(entry&0xffffu);n<end;++n) {
                uint j=(lightWords[indices+n/8u][(n/2u)%4u]>>(16u*(n&1u)))&0xffffu;
                if(!flashPasses(first+2u*j,origin3,delta,low,high)) return 0.0;
            }
        }
    } else for(uint j=first;j<first+2u*count;j+=2u) if(!flashPasses(j,origin3,delta,low,high)) return 0.0;
    return extent>0.0?amount*(high-low)/(2.0*extent):amount;
}
float flashAt(vec3 point,vec3 normal,uint i) {return flashAt(point,normal,i,0.0);}
float flashAt(vec3 point,uint i) {return flashAt(point,vec3(0),i,0.0);}
// The stipple threshold for a point, the same for every light, so callers
// work it out once outside their light loops. Anchored to the surface, not
// the screen: on floors and ceilings by map x/y, on walls by distance along
// the wall and height, on sprites (normal vec3(0)) across the view. A cell
// is two map units (a chunky pair of texels of unscaled artwork), grown in
// powers of two to stay at least a Doom pixel far off, so the pattern holds
// still while the swaying beam's edge moves over it. A 2x2 pattern: its four
// steps read as clean dot patterns where 4x4's sixteen looked like noise.
// 0 when off.
float flashStipple(vec3 point,vec3 normal) {
    if((uint(c.flashlightTint.w)&FEATURE_STIPPLED_SHADOWS)==0u) return 0.0;
    vec2 anchor;
    if(abs(normal.z)>0.5) anchor=point.xy;
    else if(dot(normal.xy,normal.xy)>0.0) anchor=vec2(dot(point.xy,normalize(vec2(-normal.y,normal.x))),point.z);
    else anchor=vec2(dot(point.xy,normalize(c.right.xy)),point.z);
    float depth=max(dot(point-c.eye.xyz,c.forward.xyz),1.0);
    float pixel=depth/(max(c.projection.y,0.01)*100.0);
    float cell=exp2(max(1.0,ceil(log2(pixel))));
    ivec2 q=ivec2(floor(anchor/cell))&1;
    float level=float(2*(q.x^q.y)+q.y); // Bayer's 2x2: 0 2 / 3 1
    return (level+0.5)/2.0-1.0;
}
#endif
// Lambert facing, blended toward 1 by the light's directionality (color.w):
// near-surface emissive sources stay soft so they still light their own wall.
float flashFacing(vec3 point,vec3 normal,uint i) {
    vec3 toLight=lights[i].position.xyz-point;
    float distance=length(toLight);
    float lambert=distance>0.001?max(0.0,dot(normal,toLight)/distance):1.0;
    return mix(1.0,lambert,lights[i].color.w);
}
// FEATURE_CLASSIC_FLASHLIGHT: the flashlight (the only light with a cone)
// works like Doom's extralight. Its light comes in hard 1/8 steps, and the
// world shader lifts illumination toward BEAM_CEILING by it, never past: a
// tick above full brightness, so the beam reads without washing colors out.
#define BEAM_STEPS 8.0
#define BEAM_CEILING 1.2
bool classicBeam(uint i) {return (uint(c.flashlightTint.w)&FEATURE_CLASSIC_FLASHLIGHT)!=0u&&lights[i].direction.w>0.0;}
float beamSteps(float amount) {return floor(saturate(amount)*BEAM_STEPS+0.5)/BEAM_STEPS;}
// Fog: analytic exponential height extinction, an analytic headlamp glow, and
// one soft ray/sphere glow per nearby light. No marching through the volume.
// Use the denser endpoint for downward rays to avoid an overflowing exp(-x).
// Density thickens exponentially below the median floor, but stops 96 units
// down: deep shafts (Freedoom has some hundreds of units deep) would
// otherwise fog solid. Below that the ray sees the capped density.
float heightFogTau(float oz,float pz,float distance,float density,float falloff,float reference) {
    float lowest=reference-96.0,lo=min(oz,pz),hi=max(oz,pz);
    float below=hi-lo<1e-3?(lo<lowest?1.0:0.0):clamp((lowest-lo)/(hi-lo),0.0,1.0);
    float start=max(lo,lowest),x=falloff*(max(hi,lowest)-start);
    float f=x<1e-3?1.0-x*0.5+x*x/6.0:(1.0-exp(-x))/x;
    return density*distance*(below*exp(falloff*96.0)+
        (1.0-below)*exp(clamp(-falloff*(start-reference),-80.0,80.0))*f);
}
float fogBeamIntegral(float distance,float radius) {
    float u=clamp(distance/max(radius,0.001),0.0,1.0);
    return radius*(u-u*u*u+0.5*u*u*u*u);
}
uint fogLight(uint n) {return n==0u?fogA.y:n==1u?fogA.z:n==2u?fogA.w:fogB.x;}
vec4 fogAlong(vec3 end) {
    float distance=length(end-c.eye.xyz);
    if(c.materials.z<=0.0||c.effects.z!=0.0||distance<0.01) return vec4(0,0,0,1);
    vec3 ray=(end-c.eye.xyz)/distance;
    float transmission=exp(-heightFogTau(c.eye.z,end.z,distance,c.fog.x,c.fog.y,c.fog.z));
    distance=min(distance,2000.0); // Bound only the approximate light-glow work.
    vec3 glow=fogHaze(c.fx.yzw)*(1.0-transmission);
#ifndef NO_LOCAL_FOG
    for(uint n=0u;n<fogA.x;++n) {
        uint i=fogLight(n);
        vec3 offset=lights[i].position.xyz-c.eye.xyz;
        float radius=lights[i].position.w,energy=0.0,center=0.0;
        if(lights[i].direction.w>0.0&&dot(offset,offset)<1600.0) {
#ifdef NO_BEAM_FOG
            continue;
#endif
            // The handheld beam starts within a few dozen units of the eye, so
            // its angle is nearly constant along a view ray. Integrate its
            // radial fade directly; geometry already ends it.
            float cone=smoothstep(lights[i].direction.w,0.985,dot(ray,lights[i].direction.xyz));
            energy=lights[i].strength*cone*fogBeamIntegral(distance,radius);
            center=min(distance,radius)*0.3;
        } else {
            float along=dot(offset,ray);
            float perpendicular2=max(0.0,dot(offset,offset)-along*along);
            float chord2=radius*radius-perpendicular2;
            if(chord2<=0.0) continue;
            float chord=sqrt(chord2);
            float near=max(0.0,along-chord),far=min(distance,along+chord);
            if(far<=near) continue;
            center=clamp(along,near,far);
            // Unoccluded on purpose: a portal test per pixel and fog light
            // doubled the fog cost. Geometry still ends each ray at walls.
            float amount=flashAtOpen(c.eye.xyz+ray*center,i);
            energy=amount*(far-near)*0.5;
        }
        glow+=lights[i].color.rgb*energy*(0.00035*0.38)*exp(-heightFogTau(c.eye.z,c.eye.z+ray.z*center,center,c.fog.x,c.fog.y,c.fog.z));
    }
#endif
    return vec4(glow,transmission);
}
