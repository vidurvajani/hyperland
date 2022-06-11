#include "defines.hpp"
#include "debug/Log.hpp"
#include "Compositor.hpp"
#include "config/ConfigManager.hpp"
#include "init/initHelpers.hpp"
#include <iostream>

// I am a bad bad boy and have used some global vars here,
// just for this file
bool ignoreSudo = false;

int main(int argc, char** argv) {

    if (!getenv("XDG_RUNTIME_DIR"))
        RIP("XDG_RUNTIME_DIR not set!");

    // parse some args
    if (!strcmp(argv[1], "i-am-really-stupid"))
        ignoreSudo = true;
    else if (!strcmp(argv[1], "help"))
        std::cout << "Available commands:\n";
        std::cout << "  help                 - show this help text\n";
        std::cout << "  version              - show version\n";
        std::cout << "  --i-am-really-stupid - ignore sudo run check\n";
        return 0;
    else if (!strcmp(argv[1], "vesrsion"))
        std::cout << "V5.0\n";
        return 0;
    else
        std::cout << "That argument does not exist\n";
        std::cout << "For a list of available arguments run hyprland help\n";
        return 1;

    system("mkdir -p /tmp/hypr");

    if (!ignoreSudo) {
        if (Init::isSudo()) {
            std::cout << "Hyprland shall not be run as the root user. If you really want to, use the i-am-really-stupid flag.\n";
            return 1;
    	}
    } else {
        std::cout << "Running with ignored root checks, I surely hope you know what you're doing.\n";
        sleep(1);
    }

    std::cout << "Welcome to Hyprland!\n";

    // let's init the compositor.
    // it initializes basic Wayland stuff in the constructor.
    g_pCompositor = std::make_unique<CCompositor>(); 

    Debug::log(LOG, "Hyprland init finished.");

    // If all's good to go, start.
    g_pCompositor->startCompositor();

    // If we are here it means we got yote.
    Debug::log(LOG, "Hyprland reached the end.");

    g_pCompositor->cleanupExit();

    return EXIT_SUCCESS;
}
