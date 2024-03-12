/*
    SPDX-FileCopyrightText: 2015 Vishesh Handa <vhanda@kde.org>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "filecontentindexerprovider.h"

#include "transaction.h"
#include "database.h"

using namespace Baloo;

FileContentIndexerProvider::FileContentIndexerProvider(Database* db)
    : m_db(db)
{
}

QVector<quint64> FileContentIndexerProvider::fetch(uint size)
{
    Transaction tr(m_db, Transaction::ReadOnly);
    return tr.fetchPhaseOneIds(size);
}

uint FileContentIndexerProvider::size()
{
    Transaction tr(m_db, Transaction::ReadOnly);
    return tr.phaseOneSize();
}

bool FileContentIndexerProvider::markFailed(const QString &url)
{
    Transaction tr(m_db, Transaction::ReadWrite);
    quint64 id = tr.documentId(QFile::encodeName(url));
    if (!id) {
        return false;
    }
    if (!tr.hasFailed(id)) {
        tr.addFailed(id);
    }
    tr.removePhaseOne(id);
    return tr.commit();
}
