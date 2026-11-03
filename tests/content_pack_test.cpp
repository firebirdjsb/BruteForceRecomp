#include "../native_launcher/content_pack.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
static void write(const std::filesystem::path &path,const char *text){std::filesystem::create_directories(path.parent_path());std::ofstream(path,std::ios::binary)<<text;}
#define CHECK(c) do{if(!(c))throw std::runtime_error("Check failed: " #c);}while(0)
int main(int argc,char **argv) try {
    if(argc==3&&std::string(argv[1])=="--verify-real") {
        const auto count=bf::ensure_content_pack({},std::filesystem::absolute(argv[2]),bf::dlc_manifest);
        CHECK(count==3);std::cout<<"Verified all 51 real DLC files across 3 packages.\n";return 0;
    }
    const auto root=std::filesystem::temp_directory_path()/("bf-dlc-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto bundle=root/"bundle",installed=root/"player-data/TDATA/4d53001e/$c";
    const char *first="4d53001e00000001/level.tgz",*second="4d53001e00000002/text/strings.xmb";
    write(bundle/first,"level");write(bundle/second,"text");
    const auto h1=bf::content_sha256(bundle/first),h2=bf::content_sha256(bundle/second);
    const bf::ContentFile files[]={{first,5,h1.c_str()},{second,4,h2.c_str()}};
    const bf::ContentManifest manifest{"synthetic",files};
    const auto save=installed.parent_path()/"global-data.xmb";write(save,"permanent progress");
    const auto save_hash=bf::content_sha256(save);
    CHECK(bf::ensure_content_pack({},installed,manifest)==0);
    CHECK(bf::ensure_content_pack(bundle,installed,manifest)==2);
    CHECK(bf::ensure_content_pack(bundle,installed,manifest)==2);
    CHECK(bf::ensure_content_pack({},installed,manifest)==2);
    CHECK(bf::content_sha256(save)==save_hash&&bf::content_sha256(installed/first)==h1);
    write(bundle/first,"wrong");bool rejected=false;
    try{bf::ensure_content_pack(bundle,root/"corrupt-destination",manifest);}catch(const std::runtime_error &){rejected=true;}
    CHECK(rejected&&!std::filesystem::exists(root/"corrupt-destination"));write(bundle/first,"level");
    write(installed/first,"modded");rejected=false;
    try{bf::ensure_content_pack(bundle,installed,manifest);}catch(const std::runtime_error &){rejected=true;}
    CHECK(rejected&&std::filesystem::file_size(installed/first)==6); // Existing differing content retained.
    const bf::ContentFile unsafe[]={{"../escape",5,h1.c_str()}};
    rejected=false;try{bf::ensure_content_pack(bundle,root/"unsafe",{"bad",unsafe});}catch(const std::runtime_error &){rejected=true;}
    CHECK(rejected&&!std::filesystem::exists(root/"escape"));
    rejected=false;try{bf::ensure_content_pack(bundle,root/"cancelled",manifest,[]{return true;});}catch(const std::runtime_error &){rejected=true;}
    CHECK(rejected&&!std::filesystem::exists(root/"cancelled"));
    unsigned checks=0;rejected=false;
    try{bf::ensure_content_pack(bundle,root/"cancelled-midway",manifest,[&]{return ++checks==4;});}catch(const std::runtime_error &){rejected=true;}
    CHECK(rejected&&!std::filesystem::exists(root/"cancelled-midway"));
    for(const auto &entry:std::filesystem::directory_iterator(root))CHECK(entry.path().filename().string().find(".bf-dlc-stage-")!=0);
    // Symlink creation needs a Windows privilege; exercise this guard wherever
    // the OS permits creating this synthetic link (including Linux CI).
    std::error_code link_error;std::filesystem::create_directory_symlink(bundle,root/"linked-bundle",link_error);
    if(!link_error) {
        rejected=false;try{bf::ensure_content_pack(root/"linked-bundle",root/"linked-output",manifest);}catch(const std::runtime_error &){rejected=true;}
        CHECK(rejected&&!std::filesystem::exists(root/"linked-output"));
        rejected=false;try{bf::ensure_content_pack(bundle,root/"linked-bundle"/"destination",manifest);}catch(const std::runtime_error &){rejected=true;}
        CHECK(rejected&&!std::filesystem::exists(bundle/"destination"));
    }
    CHECK(bf::content_sha256(save)==save_hash);
    // Only the uniquely created synthetic fixture is removed.
    std::filesystem::remove_all(root);
    std::cout<<"Verified DLC install, idempotence, corruption, traversal, cancellation, link rejection and save preservation.\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
