#include <stdexcept>
#include <cstdio>
static int M_CheckParm(char*) {return 0;}
#include "../platform/scene3d/frame_workers.h"
int main() {
    for(int lifetime=0;lifetime<4;++lifetime) {
        const unsigned limits[]={1,2,4,8};
        doom3d::FrameWorkers pool(limits[lifetime]);
        for(int pass=0;pass<200;++pass) {
            int count=(pass*37)%513;
            std::vector<std::atomic<int>> hits(count);
            for(auto &hit:hits)hit=0;
            pool.each(count,[&](int n){++hits[n];});
            for(auto &hit:hits)if(hit!=1)return 1;
        }
        bool caught=false;
        try {pool.each(128,[](int n){if(n==5)throw std::runtime_error("test");});}
        catch(const std::runtime_error&){caught=true;}
        if(!caught)return 2;
        std::atomic<int> sum{0};pool.each(100,[&](int){++sum;});
        if(sum!=100)return 3;
    }
    puts("Frame workers: exactly-once jobs, barriers, exceptions, reuse and shutdown passed.");
}
