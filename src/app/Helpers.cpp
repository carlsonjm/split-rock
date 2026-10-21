// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Helpers.h"

#include "app/ChangeRunner.h"
#include "app/Notebook.h"
#include "app/Product.h"

#include <QDate>
#include <QDebug>

#include <algorithm>

namespace splitrock {

Helpers::Helpers(const QString &store, QObject *parent)
    : QObject(parent)
{
    m_runner = new ChangeRunner(store.isEmpty() ? ChangeRunner::defaultStore() : store, this);
    m_update = new SystemUpdate(this);
    addChore(QStringLiteral("update"), m_update);
    m_space = new SpaceCleanup(this);
    addChore(QStringLiteral("space"), m_space);
    m_install = new AppInstall(this);
    addChore(QStringLiteral("install"), m_install);
    m_restore = new RestorePoints(this);
    addChore(QStringLiteral("restore"), m_restore);
    m_health = new HealthCheck(this);
    addChore(QStringLiteral("health"), m_health);
    m_community = new CommunityApps(this);
    addChore(QStringLiteral("community"), m_community);
    m_tidy = new TidyUp(this);
    addChore(QStringLiteral("tidy"), m_tidy);
    m_recent = new RecentChanges(this);
    m_recent->setRecords([this] { return m_runner->records(); });
    addChore(QStringLiteral("changes"), m_recent);
    connect(m_runner, &ChangeRunner::undone, this, [this](const ChangeRunner::Record &record) {
        const bool ok = record.status == QLatin1String("undone");
        if (m_notebook)
            m_notebook->markChange(record.id, ok ? QStringLiteral("Undone %1").arg(QDate::currentDate().toString(Qt::ISODate))
                                                 : QStringLiteral("Undo failed"));
        Q_EMIT changesChanged();
        m_recent->refreshOwn();
    });
    connect(m_runner, &ChangeRunner::made, this, [this](const ChangeRunner::Record &record) {
        const bool ok = record.status == QLatin1String("done");
        if (m_notebook)
            m_notebook->markChange(record.id, ok ? QStringLiteral("Done") : QStringLiteral("Failed"));
        Q_EMIT changesChanged();
        m_recent->putBackDone(record);
    });
}

Helpers::~Helpers() = default;

void Helpers::addChore(const QString &name, Chore *chore)
{
    m_chores.append({name, chore});
    connect(chore, &Chore::changed, this, &Helpers::choreChanged);
    // Written down, with how to undo it, before the Helper changes anything.
    chore->setWriteDown([this](const QString &id, const QString &title, const QString &undo, QString *error) {
        if (!m_notebook)
            return true;
        return m_notebook->recordChange(id, title, QStringLiteral("Asked for on a card of %1's own").arg(productName()),
                                        undo, QStringLiteral("Making"), error);
    });
    // How it went, in the notebook beside what was written down.
    chore->setReport([this](const QString &id, bool ok, const QString &, const QString &) {
        if (m_notebook && !id.isEmpty())
            m_notebook->markChange(id, ok ? QStringLiteral("Done") : QStringLiteral("Failed"));
    });
}

QString Helpers::chore() const
{
    // The busy one first: it can't be closed, so it is the one shown.
    for (const auto &[name, chore] : m_chores) {
        if (chore->busy() && !chore->stage().isEmpty())
            return name;
    }
    for (const auto &[name, chore] : m_chores) {
        if (!chore->stage().isEmpty())
            return name;
    }
    return QString();
}

bool Helpers::choreBusy() const
{
    return std::any_of(m_chores.cbegin(), m_chores.cend(), [](const auto &c) { return c.second->busy(); });
}

bool Helpers::changing() const
{
    return m_runner->busy();
}

bool Helpers::openChore(const QString &asked, const QString &query)
{
    // Removing one app by its desktop file id, and Your apps, are Install an
    // app's to do.
    const bool uninstall = asked == QLatin1String("uninstall");
    const bool yours = asked == QLatin1String("apps");
    const QString name = uninstall || yours ? QStringLiteral("install") : asked;
    const auto found = std::find_if(m_chores.cbegin(), m_chores.cend(), [&name](const auto &c) { return c.first == name; });
    if (found == m_chores.cend())
        return false;
    // Never two at once: what one changes, the other may be looking at.
    if (m_runner->busy())
        return false;
    for (const auto &[other, chore] : m_chores) {
        if (other != name && chore->busy())
            return false;
    }
    if (found->second->busy())
        return true;
    for (const auto &[other, chore] : m_chores) {
        if (other != name)
            chore->close();
    }
    if (name == QLatin1String("update"))
        m_update->check();
    else if (name == QLatin1String("space"))
        m_space->check();
    else if (uninstall)
        m_install->removeApp(query);
    else if (yours)
        m_install->listYours();
    else if (name == QLatin1String("install"))
        query.isEmpty() ? m_install->ask() : m_install->search(query);
    else if (name == QLatin1String("restore"))
        m_restore->check();
    else if (name == QLatin1String("health"))
        m_health->check();
    else if (name == QLatin1String("community"))
        m_community->check();
    else if (name == QLatin1String("changes"))
        m_recent->check();
    else if (name == QLatin1String("tidy"))
        m_tidy->check();
    return true;
}

bool Helpers::undoChange(const QString &id)
{
    QString error;
    if (!m_runner->undo(id, &error)) {
        qWarning().noquote() << QStringLiteral("%1: couldn't undo %2: %3").arg(productName(), id, error);
        return false;
    }
    Q_EMIT changesChanged();
    return true;
}

bool Helpers::putBack(const QString &id)
{
    if (choreBusy() || m_runner->busy())
        return false;
    const std::optional<ChangeProposal> change = m_recent->putBack(id);
    if (!change)
        return false;
    const QString made = m_runner->make(*change, [this](const ChangeRunner::Record &record) {
        if (!m_notebook)
            return true;
        return m_notebook->recordChange(record.id, record.proposal.title,
                                        QStringLiteral("Put back from What changed, on a card of %1's own").arg(productName()),
                                        record.proposal.undo, QStringLiteral("Making"));
    });
    if (made.isEmpty())
        return false;
    m_recent->puttingBack(id);
    Q_EMIT changesChanged();
    return true;
}

} // namespace splitrock
