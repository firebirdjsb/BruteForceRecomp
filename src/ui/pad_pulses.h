#pragma once
#include <array>
#include <cstdint>
namespace bf::ui {
// A click is one complete press/release, even when several clicks arrive in
// one guest frame. Keeping a release interval prevents rapid clicks from
// becoming one held Xbox button across successive on-screen keys.
class PadPulses {
    std::array<unsigned,32> queue{};
    unsigned begin{},count{},current{},phase{};
    uint64_t until{};
public:
    void clear(){begin=count=phase=0;until=0;}
    bool busy(uint64_t now)const {return count || phase==1 || (phase==2&&now<until);}
    bool push(unsigned control) {
        if(control>15||count==queue.size())return false;
        queue[(begin+count)%queue.size()]=control;++count;return true;
    }
    bool poll(uint64_t now,unsigned &control) {
        if(phase==1) {
            if(now<until){control=current;return true;}
            phase=2;until=now+40;return false;
        }
        if(phase==2) {
            if(now<until)return false;
            phase=0;
        }
        if(!count)return false;
        current=queue[begin];begin=(begin+1)%queue.size();--count;
        phase=1;until=now+100;control=current;return true;
    }
};
}
