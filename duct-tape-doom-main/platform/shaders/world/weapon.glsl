// The relit weapon (weaponDraws, weapon_lighting.h): its surface, and the
// light from its surroundings and the lights around the player on it.
// Part of world.frag, which declares the resources these read.
// The relit weapon looking toward a light (ahead: 1 dead ahead): the light
// creeps in from the silhouette, more on the side nearer it, with a faint
// veil over the rest; glossy edges mirror it at grazing angles (mirror).
float weaponCreep(vec3 n,vec3 view,vec3 toLight,out float mirror) {
    float ahead=saturate(dot(toLight,-view));
    float edge=1.0-saturate(dot(n,view));
    float around=saturate((dot(n,toLight)+1.0)*0.5);
    mirror=pow(saturate(dot(reflect(-view,n),toLight)),12.0)*edge*ahead;
    mirror=mirror>0.4?1.4:mirror>0.15?0.5:0.0;
    return ahead*ahead*around*(0.1+0.9*edge*edge);
}
// The weapon's surroundings toward a direction in the view's frame (x
// right, y up, z ahead), blended as an ambient cube.
vec3 weaponSurroundings(vec3 d) {
    vec3 w=d*d;
    return w.x*(d.x>0.0?weaponProbe[2]:weaponProbe[3]).rgb+w.y*(d.y>0.0?weaponProbe[4]:weaponProbe[5]).rgb+
           w.z*(d.z>0.0?weaponProbe[0]:weaponProbe[1]).rgb;
}
// MODE_SCREEN|MODE_RELIT: the relit weapon (weaponDraws, weapon_lighting.h). Its
// painted light was taken out at load; its normals face the viewer, and
// its gloss sits two columns past the artwork's right edge. It stands
// about ten units ahead of the eye, where the screen pixel points. The
// sector's light falls from above: undersides and the rounded edges
// that turn away from the eye darken, in the colormaps' 1/16 steps.
// Returns the weapon's normal, the point it stands at, the direction to the
// eye and its gloss, and shades light by its normal.
struct Weapon { vec3 normal,at,view; float gloss; };
Weapon weaponSurface(inout float light) {
    vec3 weaponNormal,weaponAt,weaponView;float weaponGloss;
    ivec2 size=textureSize(image,0),p=clamp(ivec2(floor(vUV)),ivec2(0),size-1);
    int width=(size.x-2)/2;
    vec2 packed=texelFetch(image,p,0).ba*2.0-1.0;
    if((vMode&MODE_FLIPPED)!=0u) packed.x=-packed.x;
    weaponGloss=texelFetch(image,ivec2(min(p.x,width-1)+width+2,p.y),0).r;
    weaponNormal=normalize(c.right.xyz*packed.x-c.up.xyz*packed.y-c.forward.xyz*sqrt(saturate(1.0-dot(packed,packed))));
    weaponAt=c.eye.xyz+10.0*(c.forward.xyz+c.right.xyz*vWorld.x/c.projection.x+c.up.xyz*vWorld.y/c.projection.y);
    weaponView=normalize(c.eye.xyz-weaponAt);
    float edge=1.0-saturate(dot(weaponNormal,weaponView));
    light=round(light*(0.95+0.25*weaponNormal.z)*(1.0-0.4*edge*edge)*16.0)/16.0;
    return Weapon(weaponNormal,weaponAt,weaponView,weaponGloss);
}
// Lights around the player on the relit weapon (w from weaponSurface),
// added to pulse and specular; light is its shaded sector light.
void weaponLight(Weapon w,float light,inout vec3 pulse,inout vec3 specular) {
    // Lights around the player tint and shape the weapon with a
    // capped share, so they never wash out the artwork; the side
    // facing a light takes most of it, and a light ahead creeps
    // over it from the silhouette (weaponCreep). Highlights
    // come in two hard steps on the gloss map, sharper where it is
    // glossier: polish over dark metal, not an even sheen. The light
    // from above gives one too, sliding over the metal as the view
    // pitches and the weapon bobs.
    vec3 n=w.normal,view=w.view,weaponAt=w.at;float weaponGloss=w.gloss;
    float shininess=10.0+30.0*weaponGloss;
    // The sprite's rounded normals describe its camera-facing skin,
    // not the top of the barrel. Approximate that upper plane for
    // broad metal highlights, fading it out on downward-facing parts.
    float upper=smoothstep(-0.25,0.25,dot(n,c.up.xyz));
    vec3 upperNormal=normalize(c.up.xyz+0.35*n);
    float upperShininess=8.0+16.0*weaponGloss;
    // That upper plane also takes light: a lamp ahead and above falls
    // on the top of the barrel, which the eye-facing normals turn
    // away from. A lamp ahead at eye level still misses it (creep and
    // the grazing mirror cover that); the stronger of the two wins,
    // so a light both see is not counted twice. Only light from ahead
    // counts: overhead the sector's light already shades the weapon,
    // and the open sky outside would flood the barrel.
    const float upperDiffuse=0.5;
    float top=pow(saturate(dot(n,normalize(vec3(0,0,1)+view))),shininess);
    specular+=vec3(light*weaponGloss*(top>0.5?0.35:top>0.2?0.15:0.0));
    if(c.bake.z>0.0&&vStatic!=0u) {
        vec4 statics=unpackUnorm4x8(vStatic);
        vec3 toLight=normalize(statics.xyz*2.0-1.0);
        float facing=saturate(dot(n,toLight));
        float rim=pow(1.0-saturate(dot(n,view)),2.0)*facing*saturate(0.35-dot(toLight,view));
        float highlight=pow(saturate(dot(n,normalize(toLight+view))),shininess),mirror;
        float creep=weaponCreep(n,view,toLight,mirror);
        specular+=pulse*statics.w*weaponGloss*((highlight>0.5?1.6:highlight>0.2?0.6:0.0)+mirror);
        vec3 halfDirection=(toLight+view)/max(length(toLight+view),0.001);
        specular+=pulse*statics.w*weaponGloss*upper*pow(saturate(dot(upperNormal,halfDirection)),upperShininess)*0.8;
        float lit=max(facing,upper*upperDiffuse*saturate(dot(upperNormal,toLight))*smoothstep(0.1,0.6,dot(toLight,c.forward.xyz)));
        pulse*=mix(1.0,0.25+1.0*lit+1.6*rim+1.2*creep,statics.w);
    }
    // The room around it, even with no light near: a fill from each
    // side; glossy faces and every grazing edge mirror what lies
    // that way (mostly dark, bright where a lit wall is), in 1/16
    // steps; and a bright view ahead creeps over the outline.
    // surroundings: the strength of everything taken from the probe.
    const float surroundings=0.7;
    mat3 frame=transpose(mat3(c.right.xyz,c.up.xyz,c.forward.xyz));
    vec3 environment=weaponSurroundings(frame*n)*0.3;
    float edge=1.0-saturate(dot(n,view)),base=0.02+0.12*weaponGloss;
    vec3 mirrored=weaponSurroundings(frame*reflect(-view,n))*(base+(1.0-base)*pow(edge,4.0));
    specular+=round(mirrored*0.8*surroundings*16.0)/16.0;
    // Like a glossy floor, the upper metal reflects a bright panel
    // ahead. The ambient cube represents a broad source, rather than
    // a sharp point highlight. Keep the reflection in palette steps.
    vec3 upperReflection=weaponSurroundings(frame*reflect(-view,upperNormal));
    specular+=round(upperReflection*weaponGloss*upper*0.8*surroundings*16.0)/16.0;
    float aheadMirror;
    environment+=weaponProbe[0].rgb*(0.12+0.6*weaponCreep(n,view,c.forward.xyz,aheadMirror));
    specular+=weaponProbe[0].rgb*weaponGloss*aheadMirror*0.4*surroundings;
    uvec2 mask=vLightMask;
    while(any(notEqual(mask,uvec2(0)))) {
        uint word=mask.x!=0u?0u:1u;
        uint i=word*32u+uint(findLSB(mask[word]));
        mask[word]&=mask[word]-1u;
        float amount=flashAt(weaponAt,i);
        if(amount<=0.0) continue;
        amount=min(amount,1.5)*0.5;
        vec3 toLight=normalize(lights[i].position.xyz-weaponAt);
        float facing=saturate(dot(n,toLight));
        float rim=pow(1.0-saturate(dot(n,view)),2.0)*facing*saturate(0.35-dot(toLight,view));
        // A static light's light already arrives in the tint, and a
        // bright sector leaves it little headroom; its copy only
        // moves that light toward the side facing it, keeping about
        // the same total, so it shapes the weapon even in bright rooms.
        float bakedShare=c.bake.x>0.0?lights[i].baked:0.0;
        float lit=max(facing,upper*upperDiffuse*saturate(dot(upperNormal,toLight))*smoothstep(0.1,0.6,dot(toLight,c.forward.xyz)));
        pulse+=lights[i].color.rgb*amount*(0.15+1.1*lit+1.6*rim-0.6*bakedShare);
        float highlight=pow(saturate(dot(n,normalize(toLight+view))),shininess),mirror;
        // Looking toward the light, it also spills over the weapon.
        pulse+=lights[i].color.rgb*amount*1.2*weaponCreep(n,view,toLight,mirror);
        specular+=lights[i].color.rgb*amount*weaponGloss*((highlight>0.5?1.6:highlight>0.2?0.6:0.0)+mirror);
        vec3 halfDirection=(toLight+view)/max(length(toLight+view),0.001);
        specular+=lights[i].color.rgb*amount*weaponGloss*upper*pow(saturate(dot(upperNormal,halfDirection)),upperShininess)*0.8;
    }
    // Static-light redistribution may subtract its own baked tint,
    // but must not consume the independent light from the room.
    pulse=max(pulse,vec3(0))+environment*surroundings;
}
