#include "ui/upscaler_state.h"
#include "ui/graphics_options.h"
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const char *message) {
    if(!condition)throw std::runtime_error(message);
}
void environment(const char *value) {
#ifdef _WIN32
    _putenv_s("BF_UPSCALER",value);
#else
    if(*value)setenv("BF_UPSCALER",value,1);else unsetenv("BF_UPSCALER");
#endif
}
}

int main() {
    try {
        environment("");
        bf_upscaler_capabilities(0,0,0);
        bf_upscaler_initial(BF_UPSCALER_FSR3);
        require(bf_upscaler_mode()==BF_UPSCALER_FSR3,"Preferences retain the pre-probe request");
        require(!bf_upscaler_select(BF_UPSCALER_FSR3),"Unavailable temporal mode cannot be selected");
        char status[96]{};bf_upscaler_status_copy(status,sizeof status);
        require(std::strstr(status,"REQUEST FSR 3")!=nullptr&&std::strstr(status,"ACTIVE FSR 3")==nullptr,
                "A request is not labeled active before runtime status");

        bf_upscaler_capabilities(-1,1,0);
        require(bf_upscaler_capabilities_mask()==((1u<<0)|(1u<<3)),"Negative spatial capability preserves its prior value");
        bf_upscaler_spatial_supported(1);
        require((bf_upscaler_capabilities_mask()&(1u<<1))!=0,"Spatial support is independent");
        bf_upscaler_capabilities(-1,-1,1);
        const unsigned caps=bf_upscaler_capabilities_mask();
        require((caps&(1u<<1))&&(caps&(1u<<3))&&(caps&(1u<<4)),"Incremental probe keeps all discovered providers");
        require(bf_upscaler_select(BF_UPSCALER_FSR3),"Published FSR 3 capability permits selection");
        bf_upscaler_status(BF_UPSCALER_FSR3,"AMD FSR 3","");
        bf_upscaler_status_copy(status,sizeof status);
        require(std::strstr(status,"ACTIVE FSR 3")!=nullptr,"Actual provider status is surfaced");
        bf_upscaler_status(BF_UPSCALER_FSR1,"FSR 1","temporal provider unavailable");
        bf_upscaler_status_copy(status,sizeof status);
        require(std::strstr(status,"REQUEST FSR 3")&&std::strstr(status,"ACTIVE FSR 1")&&
                std::strstr(status,"temporal provider unavailable"),"Fallback remains distinct from requested mode");
        require(bf_upscaler_select(BF_UPSCALER_FSR4)&&bf_upscaler_mode()==BF_UPSCALER_FSR4,
                "FSR 4 is independently selectable");
        require(!bf_upscaler_select(2),"Invalid mode is rejected");
        char tiny[2]={'x','x'};bf_upscaler_status_copy(tiny,sizeof tiny);
        require(tiny[1]=='\0',"Status copy always terminates a bounded buffer");

        environment("fsr4");bf_upscaler_initial(BF_UPSCALER_FSR1);
        require(bf_upscaler_mode()==BF_UPSCALER_FSR4,"Explicit BF_UPSCALER overrides saved preference");
        environment("unexpected");bf_upscaler_initial(BF_UPSCALER_FSR3);
        require(bf_upscaler_mode()==BF_UPSCALER_OFF,"Unknown explicit request safely resolves to off");
        bf_upscaler_status_copy(status,sizeof status);
        require(std::strstr(status,"Unknown BF_UPSCALER request")!=nullptr,"Unknown request is visible as a fallback reason");
        environment("");

        const auto path=std::filesystem::temp_directory_path()/
            ("bf-upscaler-settings-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".settings");
        {std::ofstream file(path);file<<"BF_GRAPHICS 1\nauto d3d12 1 1 4 1080 1 1 0 70\n";}
        auto old=bf::load_graphics(path);
        require(old.fsr1&&old.temporal_fsr==0&&old.scale==4&&old.height==1080,
                "Old version-1 settings retain FSR 1 and every existing option");
        bf::save_graphics(path,old);
        auto roundtrip=bf::load_graphics(path);
        require(roundtrip.fsr1&&roundtrip.temporal_fsr==0,"FSR 1 remains in its legacy preference field");
        roundtrip.fsr1=false;roundtrip.temporal_fsr=3;roundtrip.mouse_sensitivity=2.75f;
        roundtrip.native_widescreen=true;roundtrip.volume=70;bf::save_graphics(path,roundtrip);
        auto fsr3=bf::load_graphics(path);
        require(!fsr3.fsr1&&fsr3.temporal_fsr==3&&fsr3.mouse_sensitivity==2.75f&&
                fsr3.native_widescreen&&fsr3.volume==70&&fsr3.scale==4&&fsr3.height==1080,
                "Appended version-1 field preserves FSR 3, aspect, sensitivity and existing settings");
        fsr3.temporal_fsr=4;bf::save_graphics(path,fsr3);
        require(bf::load_graphics(path).temporal_fsr==4,"Appended version-1 field persists FSR 4");
        fsr3.temporal_fsr=1;require(!bf::valid_graphics(fsr3),"Temporal preference accepts only off, FSR 3 or FSR 4");
        std::filesystem::remove(path);
        std::puts("PASS upscaler request/capability state, fallback status and append-only settings migration");
    } catch(const std::exception &error) {
        std::cerr<<"FAIL "<<error.what()<<'\n';return 1;
    }
    return 0;
}
