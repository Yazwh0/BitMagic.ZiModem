#include "zimodem_hal/fs_root.h"

#include <filesystem>
#include <mutex>
#include <random>
#include <sstream>

namespace stdfs = std::filesystem;

namespace zimodem_hal::fs
{
    namespace
    {
        std::mutex g_mutex;
        std::string g_root;

        std::string make_fresh_temp_dir()
        {
            std::random_device rd;
            std::ostringstream name;
            name << "zimodem-host-" << rd();
            stdfs::path dir = stdfs::temp_directory_path() / name.str();
            stdfs::create_directories(dir);
            return dir.string();
        }
    }

    void set_root(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_root = path;
        stdfs::create_directories(g_root);
    }

    std::string root()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_root.empty())
        {
            g_root = make_fresh_temp_dir();
        }
        return g_root;
    }

    std::string resolve(const std::string& spiffs_path)
    {
        std::string rel = spiffs_path;
        while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
            rel.erase(rel.begin());

        stdfs::path full = stdfs::path(root()) / rel;
        return full.string();
    }

    std::string reset_to_fresh_temp_dir_for_testing()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_root = make_fresh_temp_dir();
        return g_root;
    }
}
