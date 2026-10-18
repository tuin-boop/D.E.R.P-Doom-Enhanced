// Liquid floors mirroring the reflection pass.
// Part of world.frag, which declares the resources these read.
float texelLuma(ivec2 p) {
    return dot(texel(image,palette,p,true).rgb,vec3(0.299,0.587,0.114));
}
// MODE_MIRROR_FLOOR: a liquid floor mirroring the reflection pass. albedo is the
// liquid's own texel color.
void liquidMirror(inout vec4 color,vec3 albedo) {
    // The mirrored camera maps the surface to the same screen point; a
    // scrolling wobble distorts it, and grazing views reflect more.
    // Retro style steps the wobble at 12 Hz, snaps it to whole reflection
    // pixels, skips filtering and quantizes to the WAD palette.
    bool retro=(uint(c.flashlightTint.w)&FEATURE_RETRO_REFLECTIONS)!=0u;
    float motion=1.0-0.8*smoothstep(192.0,1024.0,length(vWorld-c.eye.xyz));
    float t=retro?floor(c.fog.w*12.0)/12.0:c.fog.w;
    vec2 wobble=vec2(sin(vWorld.x*0.07+t*1.9)+sin(vWorld.y*0.11+t*1.3),
                     cos(vWorld.y*0.08-t*1.6)+cos(vWorld.x*0.05+t*2.1))*(retro?0.005:0.003);
    // The reflection's alpha: how near the mirrored point lies to the
    // surface along its ray (1 touching it). Far things fade and break up.
    float near=textureLod(reflection,(gl_FragCoord.xy-c.water.xy)*c.water.zw+wobble,0.0).a;
    // The liquid's own artwork breaks the mirror up texel by texel: the
    // slope of its luminance shoves the lookup, so the image shatters on
    // Doom's texel grid and churns with the flat's animation.
    ivec2 cell=ivec2(floor(vUV));
    float here=texelLuma(cell);
    wobble+=vec2(texelLuma(cell+ivec2(1,0))-here,texelLuma(cell+ivec2(0,1))-here)*mix(0.15,0.04,near);
    // Bound distortion in reference pixels, not a fraction of the screen.
    // Large gradients used to drag distant silhouettes across the pool.
    vec2 limit=vec2(2.5*max(c.fx2.x,1.0))*c.water.zw;
    wobble=clamp(wobble,-limit,limit)*motion;
    vec2 uv=(gl_FragCoord.xy-c.water.xy)*c.water.zw+wobble;
    // Never stretch a clamped border texel into the visible floor.
    float edge=min(min(uv.x,uv.y),min(1.0-uv.x,1.0-uv.y));
    float valid=smoothstep(0.0,0.025,edge);
    near=textureLod(reflection,uv,0.0).a;
    vec3 mirrored;
    if(retro) {
        ivec2 size=textureSize(reflection,0);
        mirrored=texelFetch(reflection,clamp(ivec2(floor(uv*vec2(size))),ivec2(0),size-1),0).rgb;
        mirrored=texelFetch(paletteLUT,ivec3(round(saturate(mirrored)*31.0)),0).rgb;
    } else mirrored=textureLod(reflection,uv,0.0).rgb;
    // Murky, not glass: the liquid stains what it mirrors in its own hue,
    // and only bright things (lights, sky, fire) punch through; dim walls
    // sink into the water. Grazing views still reflect more.
    float hue=max(max(albedo.r,albedo.g),max(albedo.b,0.05));
    mirrored*=mix(vec3(1),albedo/hue,0.3);
    float bright=smoothstep(0.08,0.6,dot(mirrored,vec3(0.299,0.587,0.114)));
    float grazing=1.0-saturate(abs(normalize(c.eye.xyz-vWorld).z));
    color.rgb=mix(color.rgb,mirrored,valid*(0.18+0.42*pow(grazing,3.0))*mix(0.55,1.0,bright)*mix(0.15,1.0,near));
}
