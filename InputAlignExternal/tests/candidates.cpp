#include "CandidatePool.hpp"
#include "Shortcut.hpp"
#include <stdexcept>
void check(bool v) { if (!v) throw std::runtime_error("candidate/shortcut invariant"); }
int main() {
    std::array<int,7> states{};
    std::array<int,7> calls{};
    auto result = iax::evaluateCandidates(states,7,4,[&](int& state,int index,int tick) {
        check(state==tick); ++state; ++calls[index];
        if(index==0) return -1; // Unsupported cannot certify a branch.
        if(index==1) return 0;
        return 1;
    });
    check(result==2 && calls[0]==1 && calls[1]==1 && calls[6]==4);
    check(iax::evaluateCandidates(states,0,4,[](int&,int,int){return 1;})==-1);
    bool expired=false;
    try { iax::evaluateCandidates(states,7,4,[](int&,int,int)->int{throw 1;}); }
    catch(int) {expired=true;}
    check(expired);
    iax::Shortcut key;
    check(!key.handle(true,true,true,false).toggle);
    check(key.handle(true,true,false,true).toggle);
    check(!key.handle(true,true,true,true).toggle);
    check(key.handle(true,false,false,false).consume);
    check(key.handle(true,true,false,true).toggle);
    check(!key.handle(false,true,false,true).consume);
}
