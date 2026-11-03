#include "../native_launcher/instance.h"
#include "../native_launcher/process.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

int main(int argc,char **argv) try {
    namespace fs=std::filesystem;
    if(argc==3&&(std::string(argv[1])=="--try-lock"||std::string(argv[1])=="--exit-lock")) {
        bf::LauncherInstance instance(bf::text_path(argv[2]));
        if(instance.owns_library()&&std::string(argv[1])=="--exit-lock")std::_Exit(7);
        return instance.owns_library()?0:2;
    }
    const auto temporary=fs::canonical(fs::temp_directory_path());
    const auto root=temporary/("bf-instance-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto exe=fs::absolute(bf::text_path(argv[0]));
    fs::create_directories(root);
    const auto try_child=[&]{return bf::run_process(exe,{"--try-lock",bf::path_text(root)},root,root/"child.log").code;};
    {
        bf::LauncherInstance first(root),duplicate(root),independent(root/"other-library");
        if(!first.owns_library()||duplicate.owns_library()||!independent.owns_library())return 1;
        if(try_child()!=2)return 2;
    }
    if(try_child()!=0)return 3;
    // Stale PID metadata must not prevent the next process acquiring the lock.
    if(try_child()!=0)return 4;
    if(bf::run_process(exe,{"--exit-lock",bf::path_text(root)},root,root/"child.log").code!=7||try_child()!=0)return 5;
    fs::remove(root/"child.log");fs::remove(root/"launcher.lock");
    fs::remove(root/"other-library/launcher.lock");fs::remove(root/"other-library");fs::remove(root);
    std::puts("Duplicate processes share a library lock, independent libraries coexist, and exit releases ownership.");
    return 0;
} catch(const std::exception &error){std::fprintf(stderr,"%s\n",error.what());return 10;}
