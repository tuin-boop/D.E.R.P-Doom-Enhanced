#include "../platform/completion_notices.h"
#include <stdexcept>
#include <iostream>
static void check(bool ok){if(!ok)throw std::runtime_error("completion notice regression");}
int main(){try{
    doomnotice::Completion c;
    check(c.update({0,0,0},{10,2,5})==0);
    check(c.update({10,0,0},{10,2,5})==1);
    check(c.update({10,0,0},{10,2,5})==0);
    check(c.update({10,2,5},{10,2,5})==14);
    check(c.update({11,2,5},{10,2,5})==0);
    c.reset();check(c.update({0,0,0},{1,1,1})==0);
    check(c.update({1,1,1},{1,1,1})==15);
    // A save loaded after completion must not replay it.
    c.reset();check(c.update({1,1,1},{1,1,1})==0);
    check(c.update({1,1,1},{1,1,1})==0);
    c.reset();check(c.update({0,0,0},{0,0,0})==0);
    check(c.update({0,0,0},{0,0,0})==0);
    c.reset();check(c.update({0,0,0},{1,0,0})==0);
    check(c.update({1,0,0},{1,0,0})==9);
    check(c.update({0,0,0},{1,0,0})==0);
    check(c.update({1,0,0},{1,0,0})==0);
    std::cout<<"Completion milestones, simultaneous awards, empty categories, load and repeat checks passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
