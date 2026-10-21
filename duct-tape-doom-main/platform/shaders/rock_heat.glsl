// A continuous three-dimensional heat field, independent of mesh edges.
float rockHash(vec3 p){return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453);}
float rockNoise(vec3 p){
 vec3 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
 return mix(mix(mix(rockHash(i),rockHash(i+vec3(1,0,0)),f.x),mix(rockHash(i+vec3(0,1,0)),rockHash(i+vec3(1,1,0)),f.x),f.y),
            mix(mix(rockHash(i+vec3(0,0,1)),rockHash(i+vec3(1,0,1)),f.x),mix(rockHash(i+vec3(0,1,1)),rockHash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
// Linear lattice interpolation produces angular fractures rather than rounded loops.
float fractureNoise(vec3 p){
 vec3 i=floor(p),f=fract(p);
 // Tetrahedral interpolation keeps each fracture segment planar.
 vec3 axisA=vec3(1,0,0),axisB=vec3(0,1,0),axisC=vec3(0,0,1);
 float a=f.x,b=f.y,c=f.z,t;vec3 axis;
 if(a<b){t=a;a=b;b=t;axis=axisA;axisA=axisB;axisB=axis;}
 if(b<c){t=b;b=c;c=t;axis=axisB;axisB=axisC;axisC=axis;}
 if(a<b){t=a;a=b;b=t;axis=axisA;axisA=axisB;axisB=axis;}
 return (1.0-a)*rockHash(i)+(a-b)*rockHash(i+axisA)
       +(b-c)*rockHash(i+axisA+axisB)+c*rockHash(i+vec3(1));
}
float fracture(vec3 p){
 float mainCrack=abs(fractureNoise(p*.19+8.0)-.49);
 float branch=abs(fractureNoise(p*.39+31.0)-.50);
 return min(mainCrack,max(branch+.012,mainCrack*.45));
}
vec4 rockHeat(){
 vec3 p=vec3(vUV,vSun.x);
 {
     bool thermal=(vMode&1024u)!=0u;
     float reach=1.0-smoothstep(vSun.y*.45,vSun.y,length(p));
     float temperature=sqrt(float(vStatic&65535u)/65535.0);
     // The mask stays fixed to the rock; cooling progressively seals its aperture.
     float width=thermal?mix(.005,.038,temperature*temperature):mix(.008,.032,temperature);
     float originalVein=fracture(p);
     float aa=max(.001,min(.035,fwidth(originalVein)));
     float aperture=1.0-smoothstep(width-aa,width+aa,originalVein);
     float detail=1.0-smoothstep(100.0,260.0,length(vToEye));
     vec3 n=normalize(cross(dFdx(vWorld),dFdy(vWorld)));
     vec3 eye=normalize(vToEye);
     if(dot(n,eye)<0.0)n=-n;
     // Four bounded relief steps, at most 0.8 map units deep. Grazing views
     // are clamped and the original aperture masks the inset material.
     vec3 tangent=(eye-n*dot(n,eye))/max(.4,dot(n,eye));
     vec3 inset=p;
     float depth=0.0;
     if(detail>.01&&aperture>.01){
         for(int step=0;step<4;++step){
             float cavity=.8*(1.0-smoothstep(.015,.10,fracture(inset)))*detail;
             if(depth>=cavity)break;
             depth+=.2*detail;
             inset=p-tangent*depth;
         }
     }
     float vein=fracture(inset);
     float core=1.0-smoothstep(width*.25,width+aa,vein);
     float lip=1.0-smoothstep(.035,.12,originalVein);
     float grain=mix(.5,rockNoise(inset*2.8+4.0),detail);
     float pockets=smoothstep(.25,.50,rockNoise(p*.075+7.0));
     if((vMode&256u)!=0u){
         // Height-gradient normal shading on the crust's chipped bevel.
         float h=1.0-lip;
         vec3 dpdx=dFdx(vWorld),dpdy=dFdy(vWorld);
         vec3 r1=cross(dpdy,n),r2=cross(n,dpdx);
         float det=dot(dpdx,r1);
         vec3 grad=(r1*dFdx(h)+r2*dFdy(h))/max(abs(det),.0001)*sign(det);
         vec3 bumped=normalize(n-grad*.65*detail);
         float lighting=.65+.35*max(0.0,dot(bumped,normalize(vec3(.3,-.4,.85))));
         float relief=mix((.10+.06*grain)*lighting,.008,lip);
         return vec4(vec3(relief)*(thermal?vec3(1.0,.82,.67):vec3(.83,.91,1.0)),vLight*reach*(.64+.25*lip)*pockets);
     }
     float hotspots=smoothstep(.59,.82,grain)*temperature;
     vec3 molten=mix(vec3(vTint.r,vTint.r*.06,.001),
                     vec3(vTint.r,vTint.r*.78,vTint.r*.25),hotspots);
     if(!thermal){
         float peak=max(max(vTint.r,vTint.g),vTint.b);
         molten=mix(vTint,vec3(peak),hotspots*.65);
     }
     float exposed=smoothstep(mix(.64,.22,temperature),.78,grain);
     // The crust occludes the recessed glow; small hot islands survive cooling.
     return vec4(molten,vLight*reach*pockets*aperture*core*(.12+1.25*exposed));
 }
}
