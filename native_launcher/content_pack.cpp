#include "content_pack.h"
#include "xr_sha256.h"
#include <array>
#include <chrono>
#include <fstream>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace bf {
std::string content_sha256(const std::filesystem::path &path) {
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot read DLC file: "+path.filename().string());
    xr_sha256 hash;xr_sha256_init(&hash);std::array<char,65536> buffer;
    while(file){file.read(buffer.data(),buffer.size());xr_sha256_update(&hash,buffer.data(),size_t(file.gcount()));}
    if(file.bad())throw std::runtime_error("DLC file read failed");
    uint8_t digest[32];char hex[65];xr_sha256_final(&hash,digest);xr_sha256_hex(digest,hex);return hex;
}
namespace {
std::filesystem::path checked(const std::filesystem::path &root,const std::filesystem::path &relative) {
    if(relative.empty()||relative.is_absolute()||relative.has_root_name())throw std::runtime_error("Unsafe DLC path");
    auto path=root;
    for(const auto &part:relative) {
        const auto name=part.string();
        if(name.empty()||name=="."||name==".."||name.find_first_of(":\\")!=std::string::npos)throw std::runtime_error("Unsafe DLC path");
        path/=part;
        std::error_code error;const auto status=std::filesystem::symlink_status(path,error);
        if(!error&&std::filesystem::is_symlink(status))throw std::runtime_error("DLC folders cannot contain symbolic links");
#ifdef _WIN32
        const auto attributes=GetFileAttributesW(path.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("DLC folders cannot contain junctions");
#endif
    }
    return path;
}
void verify(const std::filesystem::path &root,const ContentFile &file) {
    const auto path=checked(root,file.path);
    if(!std::filesystem::is_regular_file(path)||std::filesystem::file_size(path)!=file.size||content_sha256(path)!=file.sha256)
        throw std::runtime_error("DLC verification failed: "+std::string(file.path));
}
}
unsigned ensure_content_pack(const std::filesystem::path &bundle,const std::filesystem::path &destination,
    const ContentManifest &manifest,const std::function<bool()> &cancelled) {
    if(manifest.files.empty())return 0;
    const bool available=!bundle.empty()&&std::filesystem::is_directory(bundle);
    const auto absolute=std::filesystem::absolute(destination);
    checked(absolute.root_path(),absolute.relative_path());
    if(available){const auto source=std::filesystem::absolute(bundle);checked(source.root_path(),source.relative_path());}
    std::set<std::string> packages,missing,paths;
    for(const auto &file:manifest.files) {
        const std::filesystem::path relative(file.path);checked(destination,relative);
        const auto package=relative.begin()->string();
        if(package.size()!=16||package.find_first_not_of("0123456789abcdef")!=std::string::npos||!paths.insert(file.path).second)
            throw std::runtime_error("Invalid DLC package inventory");
        packages.insert(package);
    }
    // Validate roots too, so a pre-existing $c junction cannot redirect writes.
    if(destination.has_parent_path())checked(destination.parent_path(),destination.filename());
    if(available&&bundle.has_parent_path())checked(bundle.parent_path(),bundle.filename());
    for(const auto &package:packages) {
        const auto path=checked(destination,package);
        if(!std::filesystem::exists(path)){missing.insert(package);continue;}
        // Existing DLC and unrelated saved progress are never overwritten.
        for(const auto &file:manifest.files)if(std::filesystem::path(file.path).begin()->string()==package)verify(destination,file);
    }
    if(!available) {
        if(missing.size()==packages.size())return 0;
        if(!missing.empty())throw std::runtime_error("Bundled DLC is missing; the existing installation is incomplete");
        return unsigned(packages.size());
    }
    for(const auto &file:manifest.files){if(cancelled&&cancelled())throw std::runtime_error("DLC installation cancelled");verify(bundle,file);}
    if(missing.empty())return unsigned(packages.size());
    // Stage beside the destination. Only this freshly created temporary tree
    // is removed on failure. No recursive deletion touches existing packages.
    const auto parent=destination.parent_path();std::filesystem::create_directories(parent);
    const auto staging=parent/(".bf-dlc-stage-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!std::filesystem::create_directory(staging))throw std::runtime_error("Cannot create DLC staging directory");
    struct Stage {std::filesystem::path path;~Stage(){std::error_code error;std::filesystem::remove_all(path,error);}} cleanup{staging};
    for(const auto &file:manifest.files) {
        if(!missing.count(std::filesystem::path(file.path).begin()->string()))continue;
        if(cancelled&&cancelled())throw std::runtime_error("DLC installation cancelled");
        const auto to=checked(staging,file.path);std::filesystem::create_directories(to.parent_path());
        std::filesystem::copy_file(checked(bundle,file.path),to);verify(staging,file);
    }
    std::filesystem::create_directories(destination);
    for(const auto &package:missing) {
        if(cancelled&&cancelled())throw std::runtime_error("DLC installation cancelled");
        std::filesystem::rename(checked(staging,package),checked(destination,package));
    }
    for(const auto &file:manifest.files)verify(destination,file);
    return unsigned(packages.size());
}
}
