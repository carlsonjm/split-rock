// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace splitrock {

// Split Rock Settings' one page, Ask (docs/ASK.md, Split Rock Settings): Ask
// on or off, who answers, the model and Codex's sign-in, for a computer with
// or without Shuffle. Ask's switch is its line in split-rock-corerc; the rest
// goes through Ask on the session bus, which the bus starts on the first call.
class AskSettings : public QObject
{
    Q_OBJECT
    // Ask is installed where the bus can start it.
    Q_PROPERTY(bool installed READ installed CONSTANT)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    // missing, downloading, verifying, ready or failed.
    Q_PROPERTY(QString state READ state NOTIFY modelChanged)
    Q_PROPERTY(double progress READ progress NOTIFY modelChanged)
    Q_PROPERTY(QString name READ name NOTIFY modelChanged)
    Q_PROPERTY(QString size READ size NOTIFY modelChanged)
    Q_PROPERTY(QString credit READ credit NOTIFY modelChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY modelChanged)
    // llama-server, which runs the model, is on this computer.
    Q_PROPERTY(bool engine READ engine NOTIFY modelChanged)
    Q_PROPERTY(bool codexInstalled READ codexInstalled NOTIFY codexChanged)
    Q_PROPERTY(bool codexSignedIn READ codexSignedIn NOTIFY codexChanged)
    Q_PROPERTY(bool codexOnly READ codexOnly NOTIFY codexChanged)
    Q_PROPERTY(QString codexProblem READ codexProblem NOTIFY codexChanged)

public:
    static constexpr char Service[] = "io.github.carlsonjm.splitrock.Ask";

    // settings: split-rock-corerc, where Ask is switched off.
    explicit AskSettings(const QString &settings, QObject *parent = nullptr);

    bool installed() const { return m_installed; }
    bool enabled() const;
    void setEnabled(bool on);
    QString state() const { return m_model.value(QStringLiteral("state")).toString(); }
    double progress() const { return m_model.value(QStringLiteral("progress")).toDouble(); }
    QString name() const { return m_model.value(QStringLiteral("name")).toString(); }
    QString size() const { return m_model.value(QStringLiteral("size")).toString(); }
    QString credit() const { return m_model.value(QStringLiteral("credit")).toString(); }
    QString problem() const { return m_model.value(QStringLiteral("problem")).toString(); }
    bool engine() const { return m_model.value(QStringLiteral("engine"), true).toBool(); }
    bool codexInstalled() const { return m_codex.value(QStringLiteral("installed")).toBool(); }
    bool codexSignedIn() const { return m_codex.value(QStringLiteral("signedIn")).toBool(); }
    bool codexOnly() const { return m_codex.value(QStringLiteral("only")).toBool(); }
    QString codexProblem() const { return m_codex.value(QStringLiteral("problem")).toString(); }

    // Reads Ask's model and Codex afresh, as the window opens.
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setCodexOnly(bool only);
    // Each only after the person's tap.
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void remove();
    // Codex's own window, on its sign-in.
    Q_INVOKABLE void signInToCodex();
    Q_INVOKABLE void signOutOfCodex();

Q_SIGNALS:
    void enabledChanged();
    void modelChanged();
    void codexChanged();

private Q_SLOTS:
    void takeModel(const QVariantMap &model);
    void takeCodex(const QVariantMap &codex);

private:
    void call(const QString &method, const QVariantList &arguments = {});

    QString m_settings;
    bool m_installed = false;
    QVariantMap m_model;
    QVariantMap m_codex;
};

} // namespace splitrock
