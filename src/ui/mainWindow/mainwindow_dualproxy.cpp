// Dual Proxy: the profile-list actions that assign, re-port and clear the secondary proxy.
#include "include/ui/mainwindow.h"

#include <QInputDialog>

#include "include/configs/generate.h"
#include "include/database/ProfilesRepo.h"
#include "include/engine/connection/ConnectionManager.h"

void MainWindow::set_secondary_proxy() {
    const auto selected = get_now_selected_list();
    if (selected.size() != 1) return;
    const int newId = selected.first();
    auto &settings = Configs::dataManager->settingsRepo;

    if (const auto running = runningSnapshot(); running != nullptr && running->id == newId) {
        MessageBoxWarning(tr("Secondary Proxy"), tr("This profile is currently active as the primary proxy. Please select a different profile."));
        return;
    }
    // The same check the config build makes, so a profile that would be left out is refused here, with its reason.
    if (const auto problem = Configs::SecondaryProxyProblem(Configs::dataManager->profilesRepo->GetProfile(newId)); !problem.isEmpty()) {
        MessageBoxWarning(tr("Secondary Proxy"), problem);
        return;
    }

    int port = settings->secondary_inbound_port;
    if (!pick_secondary_port(&port)) return;

    const int oldId = settings->SecondaryProfileId();
    settings->secondary_inbound_port = port;
    settings->secondary_profile_id = newId;
    settings->enable_secondary_inbound = true;
    settings->Save();
    apply_secondary_change({oldId, newId});
}

void MainWindow::change_secondary_port() {
    auto &settings = Configs::dataManager->settingsRepo;
    int port = settings->secondary_inbound_port;
    if (!pick_secondary_port(&port) || port == settings->secondary_inbound_port) return;
    settings->secondary_inbound_port = port;
    settings->Save();
    apply_secondary_change({settings->SecondaryProfileId()});
}

void MainWindow::clear_secondary_proxy() {
    const int oldId = Configs::dataManager->settingsRepo->SecondaryProfileId();
    if (oldId < 0) return;
    Configs::dataManager->settingsRepo->ClearSecondaryProxy();
    apply_secondary_change({oldId});
}

bool MainWindow::pick_secondary_port(int *port) {
    const auto &settings = Configs::dataManager->settingsRepo;
    int value = *port >= 1024 && *port <= 65535 ? *port : 2081;
    while (true) {
        bool ok = false;
        value = QInputDialog::getInt(this, tr("Secondary Proxy Port"), tr("Listening port for secondary proxy (mixed SOCKS/HTTP):"),
                                     value, 1024, 65535, 1, &ok);
        if (!ok) return false;
        const auto conflict = settings->SecondaryPortConflict(value);
        if (conflict.isEmpty()) break;
        MessageBoxWarning(tr("Secondary Proxy Port"), conflict);
    }
    *port = value;
    return true;
}

void MainWindow::apply_secondary_change(const QList<int> &profileIds) {
    QList<int> rows;
    for (const int id : profileIds) {
        if (id >= 0 && !rows.contains(id)) rows << id;
    }
    if (!rows.isEmpty()) refresh_proxy_list(rows);
    refresh_status();
    switch (connection()->connectionState()) {
        case ConnectionState::Running:
            connection()->restartStartedProfile();
            break;
        // The config in flight was built from the old settings; restarting now would only be refused as busy.
        case ConnectionState::Connecting:
        case ConnectionState::Stopping:
            connection()->noteRestartNeeded(Engine::RestartReason::Settings);
            break;
        case ConnectionState::Idle:
            break;
    }
}

void MainWindow::note_secondary_removed() {
    const auto &settings = Configs::dataManager->settingsRepo;
    const int served = settings->started_secondary_id;
    if (served < 0 || served == settings->SecondaryProfileId()) return;
    if (Configs::dataManager->profilesRepo->GetProfile(served) != nullptr) return;
    connection()->noteRestartNeeded(Engine::RestartReason::Profile);
}
