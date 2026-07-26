#pragma once

#include <string>

// Internal HAL API (not Arduino-shaped). Backs the SPIFFS compat layer in
// arduino/FS.h: SPIFFS's flat, absolute-path namespace ("/zphonebook.txt", "/logfile.txt",
// etc.) is mapped onto a real directory on the host filesystem, since there is no actual
// SPI flash filesystem on a PC. See docs/native-wrapper-spec.md section 7.1.
namespace zimodem_hal::fs
{
    // Sets the host directory SPIFFS paths resolve under. Creates it if missing.
    // Defaults to a temp directory the first time resolve()/root() is called if never
    // set explicitly, so tests don't require every suite to configure one.
    void set_root(const std::string& path);
    std::string root();

    // Maps a SPIFFS-style absolute path ("/foo/bar.txt") onto a real host path under
    // root(). Does not create the file; callers open it themselves.
    std::string resolve(const std::string& spiffs_path);

    // Test-only: point the root at a fresh temp directory and forget any prior root.
    std::string reset_to_fresh_temp_dir_for_testing();
}
