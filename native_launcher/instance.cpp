#include "instance.h"
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace bf {
LauncherInstance::LauncherInstance(const std::filesystem::path &workspace) {
    std::filesystem::create_directories(workspace);
    const auto path=workspace/"launcher.lock";
#ifdef _WIN32
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open the launcher library lock.");
    handle_=file;
    OVERLAPPED range{};
    owner_=LockFileEx(file,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&range)!=FALSE;
    if(!owner_ && GetLastError()!=ERROR_LOCK_VIOLATION) {
        close();throw std::runtime_error("Cannot lock the launcher library.");
    }
    // Byte 0 is the exclusive lock. The following four bytes hold the owner's
    // local PID and remain readable by a second process while byte 0 is locked.
    LARGE_INTEGER offset{};offset.QuadPart=1;DWORD copied{};
    if(SetFilePointerEx(file,offset,nullptr,FILE_BEGIN)) {
        if(owner_) {
            owner_pid_=GetCurrentProcessId();
            if(!WriteFile(file,&owner_pid_,sizeof(owner_pid_),&copied,nullptr)||copied!=sizeof(owner_pid_)) {
                close();throw std::runtime_error("Cannot publish the launcher process ID.");
            }
        } else if(!ReadFile(file,&owner_pid_,sizeof(owner_pid_),&copied,nullptr)||copied!=sizeof(owner_pid_))owner_pid_=0;
    } else {close();throw std::runtime_error("Cannot access the launcher lock metadata.");}
#else
    descriptor_=open(path.c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(descriptor_<0)throw std::runtime_error("Cannot open the launcher library lock.");
    owner_=flock(descriptor_,LOCK_EX|LOCK_NB)==0;
    if(!owner_ && errno!=EWOULDBLOCK && errno!=EAGAIN) {
        close();throw std::runtime_error("Cannot lock the launcher library.");
    }
    if(owner_) {
        owner_pid_=uint32_t(getpid());
        if(pwrite(descriptor_,&owner_pid_,sizeof(owner_pid_),1)!=sizeof(owner_pid_)) {
            close();throw std::runtime_error("Cannot publish the launcher process ID.");
        }
    }
#endif
}
LauncherInstance::~LauncherInstance(){close();}
void LauncherInstance::close() noexcept {
#ifdef _WIN32
    if(handle_) {
        if(owner_){OVERLAPPED range{};UnlockFileEx(handle_,0,1,0,&range);}
        CloseHandle(handle_);handle_=nullptr;
    }
#else
    if(descriptor_>=0){::close(descriptor_);descriptor_=-1;}
#endif
    owner_=false;
}
void LauncherInstance::activate_existing() const noexcept {
#ifdef _WIN32
    if(owner_||!owner_pid_)return;
    EnumWindows([](HWND window,LPARAM value)->BOOL {
        DWORD pid{};GetWindowThreadProcessId(window,&pid);
        if(pid!=DWORD(value))return TRUE;
        wchar_t title[128]{};GetWindowTextW(window,title,128);
        if(lstrcmpW(title,L"Brute Force Recomp - Command Center"))return TRUE;
        // A launcher hidden for the game must remain hidden. A visible but
        // minimized launcher can be restored when another shortcut is opened.
        if(IsWindowVisible(window)){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);}
        return FALSE;
    },LPARAM(owner_pid_));
#endif
}
}
