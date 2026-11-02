#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace bf {
class FilePicker {
public:
    void open(const char *title, bool iso, const std::string &current = {});
    bool draw(std::string &selected);
private:
    void scan();
    std::string title_, directory_text_, selected_, error_;
    std::filesystem::path directory_;
    std::vector<std::filesystem::directory_entry> entries_;
    bool requested_ = false, visible_ = false, iso_ = true;
    char path_[4096]{};
};
}
