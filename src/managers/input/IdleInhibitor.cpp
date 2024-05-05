#include "InputManager.hpp"
#include "../../Compositor.hpp"
#include "../../protocols/IdleInhibit.hpp"
#include "../../protocols/IdleNotify.hpp"

void CInputManager::newIdleInhibitor(std::any inhibitor) {
    const auto PINHIBIT = m_vIdleInhibitors.emplace_back(std::make_unique<SIdleInhibitor>()).get();
    PINHIBIT->inhibitor = std::any_cast<std::shared_ptr<CIdleInhibitor>>(inhibitor);

    Debug::log(LOG, "New idle inhibitor registered for surface {:x}", (uintptr_t)PINHIBIT->inhibitor->surface);

    PINHIBIT->inhibitor->listeners.destroy = PINHIBIT->inhibitor->resource.lock()->events.destroy.registerListener([this, PINHIBIT](std::any data) {
        std::erase_if(m_vIdleInhibitors, [PINHIBIT](const auto& other) { return other.get() == PINHIBIT; });
        recheckIdleInhibitorStatus();
    });

    auto WLSurface = CWLSurface::surfaceFromWlr(PINHIBIT->inhibitor->surface);

    if (!WLSurface) {
        Debug::log(LOG, "Inhibitor has no HL Surface attached to it, likely meaning it's a non-desktop element. Ignoring.");
        PINHIBIT->inert = true;
        recheckIdleInhibitorStatus();
        return;
    }

    PINHIBIT->surfaceDestroyListener = WLSurface->events.destroy.registerListener(
        [this, PINHIBIT](std::any data) { std::erase_if(m_vIdleInhibitors, [PINHIBIT](const auto& other) { return other.get() == PINHIBIT; }); });

    recheckIdleInhibitorStatus();
}

void CInputManager::recheckIdleInhibitorStatus() {

    for (auto& ii : m_vIdleInhibitors) {
        if (ii->inert)
            continue;

        auto WLSurface = CWLSurface::surfaceFromWlr(ii->inhibitor->surface);

        if (!WLSurface)
            continue;

        if (WLSurface->visible()) {
            PROTO::idle->setInhibit(true);
            return;
        }
    }

    // check manual user-set inhibitors
    for (auto& w : g_pCompositor->m_vWindows) {
        if (w->m_eIdleInhibitMode == IDLEINHIBIT_NONE)
            continue;

        if (w->m_eIdleInhibitMode == IDLEINHIBIT_ALWAYS) {
            PROTO::idle->setInhibit(true);
            return;
        }

        if (w->m_eIdleInhibitMode == IDLEINHIBIT_FOCUS && g_pCompositor->isWindowActive(w)) {
            PROTO::idle->setInhibit(true);
            return;
        }

        if (w->m_eIdleInhibitMode == IDLEINHIBIT_FULLSCREEN && w->m_bIsFullscreen && g_pCompositor->isWorkspaceVisible(w->m_pWorkspace)) {
            PROTO::idle->setInhibit(true);
            return;
        }
    }

    PROTO::idle->setInhibit(false);
    return;
}
