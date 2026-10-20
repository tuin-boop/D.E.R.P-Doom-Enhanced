/* Fixture exporter only. Reads live map/save data; never modifies the save. */
void P_ExportSolidFixture(const char *path){
    if(gameepisode!=1||gamemap!=1)I_Error("Door fixture exporter requires E1M1");
    mobj_t *player=players[0].mo;
    float origin[3]={floorf(fminf(1248,(float)unit(player->x)-40)/4)*4,-3616,-80};
    float upper[3]={ceilf(fmaxf(1472,(float)unit(player->x)+40)/4)*4,-3296,144};
    int dims[3];for(int k=0;k<3;++k)dims[k]=(int)((upper[k]-origin[k])/4)+1;
    size_t samples=(size_t)dims[0]*dims[1]*dims[2];float *base=malloc(samples*4),*field=malloc(samples*4);
    float *horizontal=malloc((size_t)dims[0]*dims[1]*numsectors*4);
    if(!base||!field||!horizontal)I_Error("Volume export allocation");
    // Signed horizontal distance to each sector polygon, once per XY node.
    for(int y=0;y<dims[1];++y)for(int x=0;x<dims[0];++x){
        float px=origin[0]+x*4,py=origin[1]+y*4;
        for(int s=0;s<numsectors;++s){sector_t *room=&sectors[s];float nearest=1e9f;int inside=0;
            for(int j=0;j<room->linecount;++j){line_t*l=room->lines[j];if(l->frontsector==l->backsector)continue;
                float ax=(float)unit(l->v1->x),ay=(float)unit(l->v1->y),bx=(float)unit(l->v2->x),by=(float)unit(l->v2->y);
                float dx=bx-ax,dy=by-ay,len=dx*dx+dy*dy,t=len>0?fmaxf(0,fminf(1,((px-ax)*dx+(py-ay)*dy)/len)):0;
                nearest=fminf(nearest,hypotf(px-ax-t*dx,py-ay-t*dy));
                if((ay>py)!=(by>py))if(px<(bx-ax)*(py-ay)/(by-ay)+ax)inside=!inside;
            }
            horizontal[((size_t)y*dims[0]+x)*numsectors+s]=inside?-nearest:nearest;
        }
    }
    indexCuts();
    for(int z=0;z<dims[2];++z)for(int y=0;y<dims[1];++y)for(int x=0;x<dims[0];++x){
        float px=origin[0]+x*4,py=origin[1]+y*4,pz=origin[2]+z*4,solid=1e9f;
        for(int s=0;s<numsectors;++s){sector_t*r=&sectors[s];if(r->ceilingheight<=r->floorheight)continue;
            float d=fmaxf(horizontal[((size_t)y*dims[0]+x)*numsectors+s],fmaxf((float)unit(r->floorheight)-pz,pz-(float)unit(r->ceilingheight)));
            solid=fminf(solid,d);
        }
        if(fabsf(solid)<.001f)solid=-.001f;
        size_t at=((size_t)z*dims[1]+y)*dims[0]+x;base[at]=solid;
        // Saved cuts become volumes, extending across mapper-owned surfaces.
        // Do not clamp to a linedef end or sector floor/ceiling here.
        for(int i=0;i<count;++i){cut_t*c=&cuts[i];if(heads[c->line*2+c->side]!=i)continue;
            double ax,ay,tx,ty;frame(c->line,c->side,&ax,&ay,&tx,&ty);
            float u=(float)((px-ax)*tx+(py-ay)*ty),d=(float)(-(px-ax)*ty+(py-ay)*tx);
            if(d<0||d>P_GlobalDigMaxDepth())continue;
            float depth=fminf((float)P_GlobalDigMaxDepth(),sampleDepthBase(c->line,c->side,u,pz,1));
            if(depth>.01f)solid=fminf(solid,fmaxf(d-depth,-d-.001f));
        }
        for(int i=0;i<flatCount;++i){flatcut_t*c=&flatCuts[i];float radius=profileRadius(flatProfile[i]),dx=(px-c->x)/radius,dy=(py-c->y)/radius,r=dx*dx+dy*dy;
            if(r>=1)continue;
            float plane=(float)unit(c->ceiling?sectors[c->sector].ceilingheight:sectors[c->sector].floorheight);
            float inward=c->ceiling?pz-plane:plane-pz,depth=c->strength*(1-r);
            if(depth>.01f&&inward>=0)solid=fminf(solid,fmaxf(inward-depth,-inward-.001f));
        }
        // Bounded test cell: intact outer skin prevents leaving the fixture.
        if(x==0||y==0||z==0||x==dims[0]-1||y==dims[1]-1||z==dims[2]-1)solid=base[at]=4;
        field[at]=solid;
    }
    FILE*f=fopen(path,"wb");if(!f)I_Error("Cannot create volume fixture");
    float step=4,spawn[3]={(float)unit(player->x),(float)unit(player->y),(float)unit(player->z)},yaw=player->angle*(6.28318530718f/4294967296.f);
    fwrite("SVL1",1,4,f);fwrite(dims,4,3,f);fwrite(&step,4,1,f);fwrite(origin,4,3,f);fwrite(spawn,4,3,f);fwrite(&yaw,4,1,f);
    fwrite(base,4,samples,f);fwrite(field,4,samples,f);fclose(f);
    printf("Exported %s: %d x %d x %d nodes, %d wall / %d flat records\n",path,dims[0],dims[1],dims[2],count,flatCount);
    free(base);free(field);free(horizontal);
}
