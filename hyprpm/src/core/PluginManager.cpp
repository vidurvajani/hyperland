#include "PluginManager.hpp"
#include "../helpers/Colors.hpp"
#include "../progress/CProgressBar.hpp"
#include "Manifest.hpp"
#include "DataState.hpp"

#include <iostream>
#include <array>
#include <filesystem>
#include <thread>
#include <fstream>
#include <algorithm>

#include <toml++/toml.hpp>

std::string execAndGet(std::string cmd) {
    cmd += " 2>&1";
    std::array<char, 128>                          buffer;
    std::string                                    result;
    const std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe)
        return "";

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

bool CPluginManager::addNewPluginRepo(const std::string& url) {

    if (DataState::pluginRepoExists(url)) {
        std::cerr << "\n" << Colors::RED << "✖" << Colors::RESET << " Could not clone the plugin repository. Repository already installed.\n";
        return false;
    }

    std::cout << Colors::GREEN << "✔" << Colors::RESET << Colors::RED << " adding a new plugin repository " << Colors::RESET << "from " << url << "\n  " << Colors::RED
              << "MAKE SURE" << Colors::RESET << " that you trust the authors. " << Colors::RED << "DO NOT" << Colors::RESET
              << " install random plugins without verifying the code and author.\n  "
              << "Are you sure? [Y/n] ";
    std::fflush(stdout);
    std::string input;
    std::getline(std::cin, input);

    if (input.size() > 0 && input[0] != 'Y' && input[0] != 'y') {
        std::cout << "Aborting.\n";
        return false;
    }

    CProgressBar progress;
    progress.m_iMaxSteps        = 5;
    progress.m_iSteps           = 0;
    progress.m_szCurrentMessage = "Cloning the plugin repository";

    progress.print();

    if (!std::filesystem::exists("/tmp/hyprpm")) {
        std::filesystem::create_directory("/tmp/hyprpm");
        std::filesystem::permissions("/tmp/hyprpm", std::filesystem::perms::all, std::filesystem::perm_options::replace);
    }

    if (std::filesystem::exists("/tmp/hyprpm/new")) {
        progress.printMessageAbove(std::string{Colors::YELLOW} + "!" + Colors::RESET + " old plugin repo build files found in temp directory, removing.");
        std::filesystem::remove_all("/tmp/hyprpm/new");
    }

    progress.printMessageAbove(std::string{Colors::RESET} + " → Cloning " + url);

    std::string ret = execAndGet("cd /tmp/hyprpm && git clone " + url + " new");

    if (!std::filesystem::exists("/tmp/hyprpm/new")) {
        std::cerr << "\n" << Colors::RED << "✖" << Colors::RESET << " Could not clone the plugin repository. shell returned:\n" << ret << "\n";
        return false;
    }

    progress.m_iSteps = 1;
    progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " cloned");
    progress.m_szCurrentMessage = "Reading the manifest";
    progress.print();

    std::unique_ptr<CManifest> pManifest;

    if (std::filesystem::exists("/tmp/hyprpm/new/hyprpm.toml")) {
        progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " found hyprpm manifest");
        pManifest = std::make_unique<CManifest>(MANIFEST_HYPRPM, "/tmp/hyprpm/new/hyprpm.toml");
    } else if (std::filesystem::exists("/tmp/hyprpm/new/hyprload.toml")) {
        progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " found hyprload manifest");
        pManifest = std::make_unique<CManifest>(MANIFEST_HYPRLOAD, "/tmp/hyprpm/new/hyprload.toml");
    }

    if (!pManifest) {
        std::cerr << "\n" << Colors::RED << "✖" << Colors::RESET << " The provided plugin repository does not have a valid manifest\n";
        return false;
    }

    if (!pManifest->m_bGood) {
        std::cerr << "\n" << Colors::RED << "✖" << Colors::RESET << " The provided plugin repository has a corrupted manifest\n";
        return false;
    }

    progress.m_iSteps = 2;
    progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " parsed manifest, found " + std::to_string(pManifest->m_vPlugins.size()) + " plugins:");
    for (auto& pl : pManifest->m_vPlugins) {
        std::string message = std::string{Colors::RESET} + " → " + pl.name + " by ";
        for (auto& a : pl.authors) {
            message += a + ", ";
        }
        if (pl.authors.size() > 0) {
            message.pop_back();
            message.pop_back();
        }
        message += " version " + pl.version;
        progress.printMessageAbove(message);
    }
    progress.m_szCurrentMessage = "Verifying headers";
    progress.print();

    const auto HEADERSSTATUS = headersValid();

    if (HEADERSSTATUS != HEADERS_OK) {

        switch (HEADERSSTATUS) {
            case HEADERS_CORRUPTED: std::cerr << "\n" << Colors::RED << "✖" << Colors::RESET << " Headers corrupted. Please run hyprpm update to fix those.\n"; break;
            case HEADERS_MISMATCHED: std::cerr << "\n" << Colors::RED << "✖" << Colors::RESET << " Headers version mismatch. Please run hyprpm update to fix those.\n"; break;
            case HEADERS_NOT_HYPRLAND: std::cerr << "\n" << Colors::RED << "✖" << Colors::RESET << " It doesn't seem you are running on hyprland.\n"; break;
            case HEADERS_MISSING: std::cerr << "\n" << Colors::RED << "✖" << Colors::RESET << " Headers missing. Please run hyprpm update to fix those.\n"; break;
            default: break;
        }

        return false;
    }

    progress.m_iSteps = 3;
    progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " Hyprland headers OK");
    progress.m_szCurrentMessage = "Building plugin(s)";
    progress.print();

    for (auto& p : pManifest->m_vPlugins) {
        std::string out;

        progress.printMessageAbove(std::string{Colors::RESET} + " → Building " + p.name);

        for (auto& bs : p.buildSteps) {
            out += execAndGet("cd /tmp/hyprpm/new && " + bs) + "\n";
        }

        if (!std::filesystem::exists("/tmp/hyprpm/new/" + p.output)) {
            std::cerr << "\n" << Colors::RED << "✖" << Colors::RESET << " Plugin " << p.name << " failed to build.\n";
            return false;
        }

        progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " built " + p.name + " into " + p.output);
    }

    progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " all plugins built");
    progress.m_iSteps           = 4;
    progress.m_szCurrentMessage = "Installing repository";
    progress.print();

    // add repo toml to DataState
    SPluginRepository repo;
    std::string       repohash = execAndGet("cd /tmp/hyprpm/new/ && git rev-parse HEAD");
    if (repohash.length() > 0)
        repohash.pop_back();
    repo.name = url.substr(url.find_last_of('/') + 1);
    repo.url  = url;
    repo.hash = repohash;
    for (auto& p : pManifest->m_vPlugins) {
        repo.plugins.push_back(SPlugin{p.name, "/tmp/hyprpm/new/" + p.output, false});
    }
    DataState::addNewPluginRepo(repo);

    progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " installed repository");
    progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " you can now enable the plugin(s) with hyprpm enable");
    progress.m_iSteps           = 5;
    progress.m_szCurrentMessage = "Done!";
    progress.print();

    std::cout << "\n";

    // remove build files
    std::filesystem::remove_all("/tmp/hyprpm/new");

    return true;
}

bool CPluginManager::removePluginRepo(const std::string& urlOrName) {
    if (!DataState::pluginRepoExists(urlOrName)) {
        std::cerr << "\n" << Colors::RED << "✖" << Colors::RESET << " Could not remove the repository. Repository is not installed.\n";
        return false;
    }

    std::cout << Colors::YELLOW << "!" << Colors::RESET << Colors::RED << " removing a plugin repository: " << Colors::RESET << urlOrName << "\n  "
              << "Are you sure? [Y/n] ";
    std::fflush(stdout);
    std::string input;
    std::getline(std::cin, input);

    if (input.size() > 0 && input[0] != 'Y' && input[0] != 'y') {
        std::cout << "Aborting.\n";
        return false;
    }

    DataState::removePluginRepo(urlOrName);

    return true;
}

eHeadersErrors CPluginManager::headersValid() {
    const auto HLVERCALL = execAndGet("hyprctl version");

    if (!HLVERCALL.contains("Tag:"))
        return HEADERS_NOT_HYPRLAND;

    std::string hlcommit = HLVERCALL.substr(HLVERCALL.find("at commit") + 10);
    hlcommit             = hlcommit.substr(0, hlcommit.find_first_of(' '));

    // find headers commit
    auto headers = execAndGet("pkg-config --cflags hyprland");

    if (!headers.contains("-I/"))
        return HEADERS_MISSING;

    headers.pop_back(); // pop newline

    std::string verHeader = "";

    while (!headers.empty()) {
        const auto PATH = headers.substr(0, headers.find(" -I/", 3));

        if (headers.find(" -I/", 3) != std::string::npos)
            headers = headers.substr(headers.find("-I/", 3));
        else
            headers = "";

        if (PATH.ends_with("protocols") || PATH.ends_with("wlroots"))
            continue;

        verHeader = PATH.substr(2) + "/hyprland/src/version.h";
        break;
    }

    if (verHeader.empty())
        return HEADERS_CORRUPTED;

    // read header
    std::ifstream ifs(verHeader);
    if (!ifs.good())
        return HEADERS_CORRUPTED;

    std::string verHeaderContent((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
    ifs.close();

    std::string hash = verHeaderContent.substr(verHeaderContent.find("#define GIT_COMMIT_HASH") + 23);
    hash             = hash.substr(0, hash.find_first_of('\n'));
    hash             = hash.substr(hash.find_first_of('"') + 1);
    hash             = hash.substr(0, hash.find_first_of('"'));

    if (hash != hlcommit)
        return HEADERS_MISMATCHED;

    return HEADERS_OK;
}

bool CPluginManager::updateHeaders() {

    const auto HLVERCALL = execAndGet("hyprctl version");

    if (!HLVERCALL.contains("Tag:")) {
        std::cerr << "\n" << Colors::RED << "✖" << Colors::RESET << " You don't seem to be running Hyprland.";
        return false;
    }

    std::string hlcommit = HLVERCALL.substr(HLVERCALL.find("at commit") + 10);
    hlcommit             = hlcommit.substr(0, hlcommit.find_first_of(' '));

    if (!std::filesystem::exists("/tmp/hyprpm")) {
        std::filesystem::create_directory("/tmp/hyprpm");
        std::filesystem::permissions("/tmp/hyprpm", std::filesystem::perms::all, std::filesystem::perm_options::replace);
    }

    if (headersValid() == HEADERS_OK) {
        std::cout << "\n" << std::string{Colors::GREEN} + "✔" + Colors::RESET + " Your headers are already up-to-date.\n";
        DataState::updateGlobalState(SGlobalState{hlcommit});
        return true;
    }

    CProgressBar progress;
    progress.m_iMaxSteps        = 5;
    progress.m_iSteps           = 0;
    progress.m_szCurrentMessage = "Cloning the hyprland repository";
    progress.print();

    if (std::filesystem::exists("/tmp/hyprpm/hyprland")) {
        progress.printMessageAbove(std::string{Colors::YELLOW} + "!" + Colors::RESET + " old hyprland source files found in temp directory, removing.");
        std::filesystem::remove_all("/tmp/hyprpm/hyprland");
    }

    progress.printMessageAbove(std::string{Colors::YELLOW} + "!" + Colors::RESET + " Cloning https://github.com/hyprwm/hyprland, this might take a moment.");

    std::string ret = execAndGet("cd /tmp/hyprpm && git clone --recursive https://github.com/hyprwm/hyprland hyprland");

    if (!std::filesystem::exists("/tmp/hyprpm/hyprland")) {
        std::cerr << "\n" << Colors::RED << "✖" << Colors::RESET << " Could not clone the hyprland repository. shell returned:\n" << ret << "\n";
        return false;
    }

    progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " cloned");
    progress.m_iSteps           = 2;
    progress.m_szCurrentMessage = "Checking out sources";
    progress.print();

    ret = execAndGet("cd /tmp/hyprpm/hyprland && git reset --hard --recurse-submodules " + hlcommit);

    progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " checked out to running ver");
    progress.m_iSteps           = 3;
    progress.m_szCurrentMessage = "Building Hyprland";
    progress.print();

    progress.printMessageAbove(std::string{Colors::YELLOW} + "!" + Colors::RESET + " configuring Hyprland");

    ret = execAndGet("cd /tmp/hyprpm/hyprland && cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Release -S . -B ./build -G Ninja");

    progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " configured Hyprland");
    progress.m_iSteps           = 4;
    progress.m_szCurrentMessage = "Installing sources";
    progress.print();

    progress.printMessageAbove(
        std::string{Colors::YELLOW} + "!" + Colors::RESET +
        " in order to install the sources, you will need to input your password.\n  If nothing pops up, make sure you have polkit and an authentication daemon running.");

    ret = execAndGet("pkexec sh \"-c\" \"cd /tmp/hyprpm/hyprland && make installheaders\"");

    // remove build files
    std::filesystem::remove_all("/tmp/hyprpm/hyprland");

    if (headersValid() == HEADERS_OK) {
        progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " installed headers");
        progress.m_iSteps           = 5;
        progress.m_szCurrentMessage = "Done!";
        progress.print();

        DataState::updateGlobalState(SGlobalState{hlcommit});

        std::cout << "\n";
    } else {
        progress.printMessageAbove(std::string{Colors::RED} + "✖" + Colors::RESET + " failed to install headers");
        progress.m_iSteps           = 5;
        progress.m_szCurrentMessage = "Failed";
        progress.print();

        std::cout << "\n";

        return false;
    }

    return true;
}

bool CPluginManager::updatePlugins(bool forceUpdateAll) {
    if (headersValid() != HEADERS_OK) {
        std::cout << "\n" << std::string{Colors::RED} + "✖" + Colors::RESET + " headers are not up-to-date, please run hyprpm update.";
        return false;
    }

    const auto REPOS = DataState::getAllRepositories();

    if (REPOS.size() < 1) {
        std::cout << "\n" << std::string{Colors::RED} + "✖" + Colors::RESET + " No repos to update.";
        return true;
    }

    const auto HLVERCALL = execAndGet("hyprctl version");

    if (!HLVERCALL.contains("Tag:")) {
        std::cerr << "\n" << Colors::RED << "✖" << Colors::RESET << " You don't seem to be running Hyprland.";
        return false;
    }

    std::string hlcommit = HLVERCALL.substr(HLVERCALL.find("at commit") + 10);
    hlcommit             = hlcommit.substr(0, hlcommit.find_first_of(' '));

    CProgressBar progress;
    progress.m_iMaxSteps        = REPOS.size() * 2 + 1;
    progress.m_iSteps           = 0;
    progress.m_szCurrentMessage = "Updating repositories";
    progress.print();

    for (auto& repo : REPOS) {
        bool update = forceUpdateAll;

        progress.m_iSteps++;
        progress.m_szCurrentMessage = "Updating " + repo.name;
        progress.print();

        progress.printMessageAbove(std::string{Colors::RESET} + " → checking for updates for " + repo.name);

        if (std::filesystem::exists("/tmp/hyprpm/update")) {
            progress.printMessageAbove(std::string{Colors::YELLOW} + "!" + Colors::RESET + " old update build files found in temp directory, removing.");
            std::filesystem::remove_all("/tmp/hyprpm/update");
        }

        progress.printMessageAbove(std::string{Colors::RESET} + " → Cloning " + repo.url);

        std::string ret = execAndGet("cd /tmp/hyprpm && git clone " + repo.url + " update");

        if (!std::filesystem::exists("/tmp/hyprpm/update")) {
            std::cout << "\n" << std::string{Colors::RED} + "✖" + Colors::RESET + " could not clone repo: shell returned:\n" + ret;
            return false;
        }

        if (!update) {
            // check if git has updates
            std::string hash = execAndGet("cd /tmp/hyprpm/update && git rev-parse HEAD");
            if (!hash.empty())
                hash.pop_back();

            update = update || hash != repo.hash;
        }

        if (!update) {
            std::filesystem::remove_all("/tmp/hyprpm/update");
            progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " repository " + repo.name + " is up-to-date.");
            progress.m_iSteps++;
            progress.print();
            continue;
        }

        // we need to update

        progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " repository " + repo.name + " has updates.");
        progress.printMessageAbove(std::string{Colors::RESET} + " → Building " + repo.name);
        progress.m_iSteps++;
        progress.print();

        std::unique_ptr<CManifest> pManifest;

        if (std::filesystem::exists("/tmp/hyprpm/update/hyprpm.toml")) {
            progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " found hyprpm manifest");
            pManifest = std::make_unique<CManifest>(MANIFEST_HYPRPM, "/tmp/hyprpm/update/hyprpm.toml");
        } else if (std::filesystem::exists("/tmp/hyprpm/update/hyprload.toml")) {
            progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " found hyprload manifest");
            pManifest = std::make_unique<CManifest>(MANIFEST_HYPRLOAD, "/tmp/hyprpm/update/hyprload.toml");
        }

        if (!pManifest) {
            std::cerr << "\n" << Colors::RED << "✖" << Colors::RESET << " The provided plugin repository does not have a valid manifest\n";
            continue;
        }

        if (!pManifest->m_bGood) {
            std::cerr << "\n" << Colors::RED << "✖" << Colors::RESET << " The provided plugin repository has a corrupted manifest\n";
            continue;
        }

        if (!pManifest->m_sRepository.commitPins.empty()) {
            // check commit pins

            progress.printMessageAbove(std::string{Colors::RESET} + " → Manifest has " + std::to_string(pManifest->m_sRepository.commitPins.size()) + " pins, checking");

            for (auto& [hl, plugin] : pManifest->m_sRepository.commitPins) {
                if (hl != hlcommit)
                    continue;

                progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " commit pin " + plugin + " matched hl, resetting");

                execAndGet("cd /tmp/hyprpm/update/ && git reset --hard --recurse-submodules " + plugin);
            }
        }

        bool failed = false;
        for (auto& p : pManifest->m_vPlugins) {
            std::string out;

            progress.printMessageAbove(std::string{Colors::RESET} + " → Building " + p.name);

            for (auto& bs : p.buildSteps) {
                out += execAndGet("cd /tmp/hyprpm/update && " + bs) + "\n";
            }

            if (!std::filesystem::exists("/tmp/hyprpm/update/" + p.output)) {
                std::cerr << "\n" << Colors::RED << "✖" << Colors::RESET << " Plugin " << p.name << " failed to build.\n";
                failed = true;
                break;
            }

            progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " built " + p.name + " into " + p.output);
        }

        if (failed)
            continue;

        // add repo toml to DataState
        SPluginRepository newrepo = repo;
        newrepo.plugins.clear();
        std::string repohash = execAndGet(
            "cd /tmp/hyprpm/update/ && git pull --recurse-submodules && git reset --hard --recurse-submodules && git rev-parse HEAD"); // repo hash in the state.toml has to match head and not any pin
        if (repohash.length() > 0)
            repohash.pop_back();
        newrepo.hash = repohash;
        for (auto& p : pManifest->m_vPlugins) {
            newrepo.plugins.push_back(SPlugin{p.name, "/tmp/hyprpm/update/" + p.output, false});
        }
        DataState::removePluginRepo(newrepo.name);
        DataState::addNewPluginRepo(newrepo);

        std::filesystem::remove_all("/tmp/hyprpm/update");

        progress.printMessageAbove(std::string{Colors::GREEN} + "✔" + Colors::RESET + " updated " + repo.name);
    }

    progress.m_iSteps++;
    progress.m_szCurrentMessage = "Done!";
    progress.print();

    std::cout << "\n";

    return true;
}

bool CPluginManager::enablePlugin(const std::string& name) {
    bool ret = DataState::setPluginEnabled(name, true);
    if (ret)
        std::cout << Colors::GREEN << "✔" << Colors::RESET << " Enabled " << name << "\n";
    return ret;
}

bool CPluginManager::disablePlugin(const std::string& name) {
    bool ret = DataState::setPluginEnabled(name, false);
    if (ret)
        std::cout << Colors::GREEN << "✔" << Colors::RESET << " Disabled " << name << "\n";
    return ret;
}

void CPluginManager::ensurePluginsLoadState() {
    if (headersValid() != HEADERS_OK) {
        std::cerr << "\n" << std::string{Colors::RED} + "✖" + Colors::RESET + " headers are not up-to-date, please run hyprpm update.";
        return;
    }

    const auto HOME = getenv("HOME");
    const auto HIS  = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!HOME || !HIS) {
        std::cerr << "PluginManager: no $HOME or HIS\n";
        return;
    }
    const auto               HYPRPMPATH = std::string(HOME) + "/.hyprpm/";

    auto                     pluginLines = execAndGet("hyprctl plugins list | grep Plugin");

    std::vector<std::string> loadedPlugins;

    std::cout << Colors::GREEN << "✔" << Colors::RESET << " Ensuring plugin load state\n";

    // iterate line by line
    while (!pluginLines.empty()) {
        auto plLine = pluginLines.substr(0, pluginLines.find("\n"));

        if (pluginLines.find("\n") != std::string::npos)
            pluginLines = pluginLines.substr(pluginLines.find("\n") + 1);
        else
            pluginLines = "";

        if (plLine.back() != ':')
            continue;

        plLine = plLine.substr(7);
        plLine = plLine.substr(0, plLine.find(" by "));

        loadedPlugins.push_back(plLine);
    }

    // get state
    const auto REPOS = DataState::getAllRepositories();

    auto       enabled = [REPOS](const std::string& plugin) -> bool {
        for (auto& r : REPOS) {
            for (auto& p : r.plugins) {
                if (p.name == plugin && p.enabled)
                    return true;
            }
        }

        return false;
    };

    auto repoForName = [REPOS](const std::string& name) -> std::string {
        for (auto& r : REPOS) {
            for (auto& p : r.plugins) {
                if (p.name == name)
                    return r.name;
            }
        }

        return "";
    };

    // unload disabled plugins
    for (auto& p : loadedPlugins) {
        if (!enabled(p)) {
            // unload
            execAndGet("hyprctl plugin unload " + HYPRPMPATH + repoForName(p) + "/" + p + ".so");
            std::cout << Colors::GREEN << "✔" << Colors::RESET << " Unloaded " << p << "\n";
        }
    }

    // load enabled plugins
    for (auto& r : REPOS) {
        for (auto& p : r.plugins) {
            if (!p.enabled)
                continue;

            if (std::find_if(loadedPlugins.begin(), loadedPlugins.end(), [&](const auto& other) { return other == p.name; }) != loadedPlugins.end())
                continue;

            execAndGet("hyprctl plugin load " + HYPRPMPATH + repoForName(p.name) + "/" + p.filename);
            std::cout << Colors::GREEN << "✔" << Colors::RESET << " Loaded " << p.name << "\n";
        }
    }

    std::cout << Colors::GREEN << "✔" << Colors::RESET << " Plugin load state ensured\n";
}