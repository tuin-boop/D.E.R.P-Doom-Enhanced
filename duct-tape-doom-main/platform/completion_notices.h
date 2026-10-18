#ifndef DOOM_COMPLETION_NOTICES_H
#define DOOM_COMPLETION_NOTICES_H
#include <array>
namespace doomnotice {
// A loaded save establishes a baseline instead of replaying old achievements.
// Empty categories satisfy overall completion but never announce themselves.
class Completion {
    unsigned awarded=0;
    bool initialized=false;
public:
    void reset(){awarded=0;initialized=false;}
    unsigned update(const std::array<int,3>& counts,const std::array<int,3>& totals){
        unsigned complete=0;
        bool all=true,any=false;
        for(unsigned i=0;i<3;++i){
            if(totals[i]>0){any=true;if(counts[i]>=totals[i])complete|=1u<<i;else all=false;}
        }
        if(any&&all)complete|=8;
        unsigned result=initialized?(complete&~awarded):0;
        awarded|=complete;initialized=true;return result;
    }
};
}
#endif
