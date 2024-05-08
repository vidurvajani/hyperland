#pragma once
#include <deque>
#include <unordered_map>

#include "../defines.hpp"
#include "../helpers/memory/SharedPtr.hpp"

struct SHyprIPCEvent {
    std::string event;
    std::string data;
};

class CEventManager {
  public:
    CEventManager();
    ~CEventManager();

    void postEvent(const SHyprIPCEvent& event);

  private:
    std::string formatEvent(const SHyprIPCEvent& event) const;

    static int  onServerEvent(int fd, uint32_t mask, void* data);
    static int  onClientEvent(int fd, uint32_t mask, void* data);

    int         onServerEvent(int fd, uint32_t mask);
    int         onClientEvent(int fd, uint32_t mask);

    struct SClient {
        std::deque<SP<std::string>> events;
        wl_event_source*            eventSource;
    };

    std::unordered_map<int, SClient>::iterator removeClientByFD(int fd);

  private:
    int                              m_iSocketFD    = -1;
    wl_event_source*                 m_pEventSource = nullptr;

    std::unordered_map<int, SClient> m_mClients;
};

inline std::unique_ptr<CEventManager> g_pEventManager;
