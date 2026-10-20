// SPDX-License-Identifier: GPL-2.0-or-later
#include "ask/AskService.h"

#include "ask/Ask.h"
#include "ask/LocalModel.h"

namespace splitrock::ask {

AskService::AskService(ask::Ask *asking, LocalModel *model, QObject *parent)
    : QObject(parent)
    , m_ask(asking)
    , m_model(model)
{
    connect(m_ask, &ask::Ask::said, this, &AskService::Said);
    connect(m_ask, &ask::Ask::answered, this, &AskService::Answered);
    connect(m_ask, &ask::Ask::modelChanged, this, &AskService::ModelChanged);
}

bool AskService::publish(QDBusConnection bus)
{
    return bus.registerObject(QLatin1String(Path), this, QDBusConnection::ExportScriptableContents);
}

uint AskService::ProtocolVersion() const
{
    return Version;
}

bool AskService::Enabled() const
{
    return m_ask->enabled();
}

uint AskService::Ask(const QString &question)
{
    return m_ask->ask(question);
}

void AskService::Cancel(uint id)
{
    m_ask->cancel(id);
}

QVariantMap AskService::Model() const
{
    return m_ask->model();
}

void AskService::DownloadModel()
{
    // Only after the person's tap, which is the caller's to ask for.
    m_model->download();
}

void AskService::CancelDownload()
{
    m_model->cancel();
}

void AskService::RemoveModel()
{
    m_model->remove();
}

} // namespace splitrock::ask
