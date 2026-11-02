#pragma once
#include "file_picker.h"
#include "library.h"
#include "theme.h"
namespace bf {
class App {
public:
    App(Library &library, Theme &theme);
    void draw();
    bool can_close();
private:
    void command();
    Library &library_;
    Theme &theme_;
    FilePicker picker_;
    bool picking_host_=false,keyboard_=true,show_exit_=false;
    char iso_[4096]{},host_[4096]{};
    std::string ui_error_;
};
}
