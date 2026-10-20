// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QVariantMap>

namespace splitrock {
class LocalModel;
}

namespace splitrock::ask {

class Ask;

// Ask on the session bus, beside Core (docs/ASK.md § On the bus). The bus
// starts the program on the first call; it ends on its own when idle.
class AskService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.carlsonjm.SplitRock.Ask")

public:
    static constexpr uint Version = 1;
    static constexpr char Service[] = "io.github.carlsonjm.splitrock.Ask";
    static constexpr char Path[] = "/Ask";

    AskService(ask::Ask *asking, LocalModel *model, QObject *parent = nullptr);

    bool publish(QDBusConnection bus);

public Q_SLOTS:
    Q_SCRIPTABLE uint ProtocolVersion() const;
    Q_SCRIPTABLE bool Enabled() const;
    // Starts answering; the words come in Said and the end in Answered, with
    // the id returned here.
    Q_SCRIPTABLE uint Ask(const QString &question);
    Q_SCRIPTABLE void Cancel(uint id);
    Q_SCRIPTABLE QVariantMap Model() const;
    Q_SCRIPTABLE void DownloadModel();
    Q_SCRIPTABLE void CancelDownload();
    Q_SCRIPTABLE void RemoveModel();

Q_SIGNALS:
    Q_SCRIPTABLE void Said(uint id, const QString &text);
    Q_SCRIPTABLE void Answered(uint id, const QVariantMap &result);
    Q_SCRIPTABLE void ModelChanged(const QVariantMap &model);

private:
    ask::Ask *m_ask;
    LocalModel *m_model;
};

} // namespace splitrock::ask
