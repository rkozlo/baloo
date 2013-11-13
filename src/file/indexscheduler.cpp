/* This file is part of the KDE Project
   Copyright (c) 2008-2010 Sebastian Trueg <trueg@kde.org>
   Copyright (c) 2010-2013 Vishesh Handa <handa.vish@gmail.com>

   Parts of this file are based on code from Strigi
   Copyright (C) 2006-2007 Jos van den Oever <jos@vandenoever.info>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "indexscheduler.h"
#include "fileindexerconfig.h"
#include "fileindexingqueue.h"
#include "basicindexingqueue.h"
#include "eventmonitor.h"
#include "indexcleaner.h"
#include "database.h"

#include <QtCore/QList>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QDirIterator>
#include <QtCore/QDateTime>
#include <QtCore/QByteArray>
#include <QtCore/QUrl>

#include <KDebug>
#include <KUrl>
#include <KStandardDirs>
#include <KConfigGroup>
#include <KLocale>

using namespace Baloo;

IndexScheduler::IndexScheduler(Database* db, QObject* parent)
    : QObject(parent)
    , m_indexing(false)
    , m_lastBasicIndexingFile(QDateTime::currentDateTime())
    , m_basicIndexingFileCount(0)
    , m_db(db)
{
    // remove old indexing error log
    if (FileIndexerConfig::self()->isDebugModeEnabled()) {
        QFile::remove(KStandardDirs::locateLocal("data", QLatin1String("nepomuk/file-indexer-error-log")));
    }

    FileIndexerConfig* indexConfig = FileIndexerConfig::self();
    connect(indexConfig, SIGNAL(includeFolderListChanged(QStringList, QStringList)),
            this, SLOT(slotIncludeFolderListChanged(QStringList, QStringList)));
    connect(indexConfig, SIGNAL(excludeFolderListChanged(QStringList, QStringList)),
            this, SLOT(slotExcludeFolderListChanged(QStringList, QStringList)));

    // FIXME: What if both the signals are emitted?
    connect(indexConfig, SIGNAL(fileExcludeFiltersChanged()),
            this, SLOT(slotConfigFiltersChanged()));
    connect(indexConfig, SIGNAL(mimeTypeFiltersChanged()),
            this, SLOT(slotConfigFiltersChanged()));

    // Stop indexing when a device is unmounted
    RemovableMediaCache* cache = new RemovableMediaCache(this);
    connect(cache, SIGNAL(deviceTeardownRequested(const RemovableMediaCache::Entry*)),
            this, SLOT(slotTeardownRequested(const RemovableMediaCache::Entry*)));

    m_basicIQ = new BasicIndexingQueue(m_db, this);
    m_fileIQ = new FileIndexingQueue(m_db, this);

    connect(m_basicIQ, SIGNAL(finishedIndexing()), this, SIGNAL(basicIndexingDone()));
    connect(m_fileIQ, SIGNAL(finishedIndexing()), this, SIGNAL(fileIndexingDone()));

    connect(m_basicIQ, SIGNAL(beginIndexingFile(QUrl)), this, SLOT(slotBeginIndexingFile(QUrl)));
    connect(m_basicIQ, SIGNAL(endIndexingFile(QUrl)), this, SLOT(slotEndIndexingFile(QUrl)));
    connect(m_basicIQ, SIGNAL(endIndexingFile(QUrl)), this, SLOT(slotEndBasicIndexingFile()));
    connect(m_fileIQ, SIGNAL(beginIndexingFile(QUrl)), this, SLOT(slotBeginIndexingFile(QUrl)));
    connect(m_fileIQ, SIGNAL(endIndexingFile(QUrl)), this, SLOT(slotEndIndexingFile(QUrl)));

    connect(m_basicIQ, SIGNAL(startedIndexing()), this, SLOT(slotStartedIndexing()));
    connect(m_basicIQ, SIGNAL(finishedIndexing()), this, SLOT(slotFinishedIndexing()));
    connect(m_fileIQ, SIGNAL(startedIndexing()), this, SLOT(slotStartedIndexing()));
    connect(m_fileIQ, SIGNAL(finishedIndexing()), this, SLOT(slotFinishedIndexing()));

    // Connect both the queues together
    connect(m_basicIQ, SIGNAL(endIndexingFile(QUrl)), m_fileIQ, SLOT(enqueue(QUrl)));

    // Status String
    connect(m_basicIQ, SIGNAL(startedIndexing()), this, SLOT(emitStatusStringChanged()));
    connect(m_basicIQ, SIGNAL(finishedIndexing()), this, SLOT(emitStatusStringChanged()));
    connect(m_fileIQ, SIGNAL(startedIndexing()), this, SLOT(emitStatusStringChanged()));
    connect(m_fileIQ, SIGNAL(finishedIndexing()), this, SLOT(emitStatusStringChanged()));
    connect(this, SIGNAL(indexingSuspended(bool)), this, SLOT(emitStatusStringChanged()));

    m_eventMonitor = new EventMonitor(this);
    connect(m_eventMonitor, SIGNAL(diskSpaceStatusChanged(bool)),
            this, SLOT(slotScheduleIndexing()));
    connect(m_eventMonitor, SIGNAL(idleStatusChanged(bool)),
            this, SLOT(slotScheduleIndexing()));
    connect(m_eventMonitor, SIGNAL(powerManagementStatusChanged(bool)),
            this, SLOT(slotScheduleIndexing()));

    m_cleaner = new IndexCleaner(this);
    connect(m_cleaner, SIGNAL(finished(KJob*)), this, SLOT(slotCleaningDone()));

    m_state = State_Normal;
    slotScheduleIndexing();
}


IndexScheduler::~IndexScheduler()
{
    m_db->commit();
}


void IndexScheduler::suspend()
{
    if (m_state != State_Suspended) {
        m_state = State_Suspended;
        slotScheduleIndexing();

        m_eventMonitor->disable();
        Q_EMIT indexingSuspended(true);
    }
}


void IndexScheduler::resume()
{
    if (m_state == State_Suspended) {
        m_state = State_Normal;
        slotScheduleIndexing();

        m_eventMonitor->enable();
        Q_EMIT indexingSuspended(false);
    }
}


void IndexScheduler::setSuspended(bool suspended)
{
    if (suspended)
        suspend();
    else
        resume();
}

bool IndexScheduler::isSuspended() const
{
    return m_state == State_Suspended;
}

bool IndexScheduler::isCleaning() const
{
    return m_state == State_Cleaning;
}

bool IndexScheduler::isIndexing() const
{
    return m_indexing;
}

QUrl IndexScheduler::currentUrl() const
{
    if (!m_fileIQ->currentUrl().isEmpty())
        return m_fileIQ->currentUrl();
    else
        return m_basicIQ->currentUrl();
}

UpdateDirFlags IndexScheduler::currentFlags() const
{
    return m_basicIQ->currentFlags();
}


void IndexScheduler::setIndexingStarted(bool started)
{
    if (started != m_indexing) {
        m_indexing = started;
        Q_EMIT indexingStateChanged(m_indexing);
        if (m_indexing)
            Q_EMIT indexingStarted();
        else
            Q_EMIT indexingStopped();
    }
}

void IndexScheduler::slotStartedIndexing()
{
    m_eventMonitor->enable();
}

void IndexScheduler::slotFinishedIndexing()
{
    m_eventMonitor->suspendDiskSpaceMonitor();
}

void IndexScheduler::slotCleaningDone()
{
    m_cleaner = 0;

    m_state = State_Normal;
    slotScheduleIndexing();
}

void IndexScheduler::updateDir(const QString& path, UpdateDirFlags flags)
{
    m_basicIQ->enqueue(path, flags);
}


void IndexScheduler::updateAll(bool forceUpdate)
{
    queueAllFoldersForUpdate(forceUpdate);
}


void IndexScheduler::queueAllFoldersForUpdate(bool forceUpdate)
{
    m_basicIQ->clear();

    UpdateDirFlags flags = UpdateRecursive | AutoUpdateFolder;
    if (forceUpdate)
        flags |= ForceUpdate;

    // update everything again in case the folders changed
    Q_FOREACH (const QString& f, FileIndexerConfig::self()->includeFolders()) {
        m_basicIQ->enqueue(f, flags);
    }
}


void IndexScheduler::slotIncludeFolderListChanged(const QStringList& added, const QStringList& removed)
{
    kDebug() << added << removed;
    Q_FOREACH (const QString& path, removed) {
        m_basicIQ->clear(path);
        m_fileIQ->clear(path);
    }

    restartCleaner();

    Q_FOREACH(const QString& path, added) {
        m_basicIQ->enqueue(path, UpdateRecursive);
    }
}

void IndexScheduler::slotExcludeFolderListChanged(const QStringList& added, const QStringList& removed)
{
    kDebug() << added << removed;
    Q_FOREACH (const QString& path, added) {
        m_basicIQ->clear(path);
        m_fileIQ->clear(path);
    }

    restartCleaner();

    Q_FOREACH (const QString &path, removed) {
        m_basicIQ->enqueue(path, UpdateRecursive);
    }
}

void IndexScheduler::restartCleaner()
{
    if (m_cleaner) {
        m_cleaner->kill();
        delete m_cleaner;
    }

    // TODO: only clean the filters that were changed from the config
    m_cleaner = new IndexCleaner(this);
    connect(m_cleaner, SIGNAL(finished(KJob*)), this, SLOT(slotCleaningDone()));

    m_state = State_Normal;
    slotScheduleIndexing();
}


void IndexScheduler::slotConfigFiltersChanged()
{
    restartCleaner();

    // We need to this - there is no way to avoid it
    m_basicIQ->clear();
    m_fileIQ->clear();

    queueAllFoldersForUpdate();
}


void IndexScheduler::analyzeFile(const QString& path)
{
    m_basicIQ->enqueue(path);
}


void IndexScheduler::slotBeginIndexingFile(const QUrl&)
{
    setIndexingStarted(true);
}

void IndexScheduler::slotEndIndexingFile(const QUrl&)
{
    const QUrl basicUrl = m_basicIQ->currentUrl();
    const QUrl fileUrl = m_fileIQ->currentUrl();

    if (basicUrl.isEmpty() && fileUrl.isEmpty()) {
        setIndexingStarted(false);
    }
}

//
// Slow down the Basic Indexing if we have > x files
//
void IndexScheduler::slotEndBasicIndexingFile()
{
    /*
    QDateTime current = QDateTime::currentDateTime();
    if (current.secsTo(m_lastBasicIndexingFile) > 60) {
        m_basicIQ->setDelay(0);
        m_basicIndexingFileCount = 0;
    } else {
        if (m_basicIndexingFileCount > 1000) {
            m_basicIQ->setDelay(400);
        } else if (m_basicIndexingFileCount > 750) {
            m_basicIQ->setDelay(300);
        } else if (m_basicIndexingFileCount > 500) {
            m_basicIQ->setDelay(200);
        } else if (m_basicIndexingFileCount > 200) {
            m_basicIQ->setDelay(100);
        } else
            m_basicIQ->setDelay(0);
    }*/

    m_basicIQ->setDelay(0);
    m_basicIndexingFileCount++;

    if ((m_basicIndexingFileCount % 100) == 0) {
        kDebug() << "Commit";
        m_db->commit();
        m_db->transaction();
    }
}


void IndexScheduler::slotTeardownRequested(const RemovableMediaCache::Entry* entry)
{
    const QString path = entry->mountPath();

    m_basicIQ->clear(path);
    m_fileIQ->clear(path);
}

void IndexScheduler::slotScheduleIndexing()
{
    if (m_state == State_Suspended) {
        kDebug() << "Suspended";
        m_basicIQ->suspend();
        m_fileIQ->suspend();
        if (m_cleaner)
            m_cleaner->suspend();
    }

    else if (m_state == State_Cleaning) {
        kDebug() << "Cleaning";
        m_basicIQ->suspend();
        m_fileIQ->suspend();
        if (m_cleaner)
            m_cleaner->resume();
    }

    else if (m_eventMonitor->isDiskSpaceLow()) {
        kDebug() << "Disk Space";
        m_state = State_LowDiskSpace;

        m_basicIQ->suspend();
        m_fileIQ->suspend();
    }

    else if (m_eventMonitor->isOnBattery()) {
        kDebug() << "Battery";
        m_state = State_OnBattery;

        m_basicIQ->setDelay(0);
        m_basicIQ->resume();

        m_fileIQ->suspend();
        if (m_cleaner)
            m_cleaner->suspend();
    }

    else if (m_eventMonitor->isIdle()) {
        kDebug() << "Idle";
        if (m_cleaner) {
            m_state = State_Cleaning;
            m_cleaner->start();
            slotScheduleIndexing();
        } else {
            m_state = State_UserIdle;
            m_basicIQ->setDelay(0);
            m_basicIQ->resume();

            m_fileIQ->setDelay(0);
            m_fileIQ->resume();
        }
    }

    else {
        kDebug() << "Normal";
        m_state = State_Normal;

        m_basicIQ->setDelay(0);
        m_basicIQ->resume();

        m_fileIQ->setDelay(3000);
        m_fileIQ->resume();
    }
}

QString IndexScheduler::userStatusString() const
{
    bool indexing = isIndexing();
    bool suspended = isSuspended();
    bool cleaning = isCleaning();
    bool processing = !m_basicIQ->isEmpty();

    if (suspended) {
        return i18nc("@info:status", "File indexer is suspended.");
    } else if (cleaning) {
        return i18nc("@info:status", "Cleaning invalid file metadata");
    } else if (indexing) {
        return i18nc("@info:status", "Indexing files for desktop search.");
    } else if (processing) {
        return i18nc("@info:status", "Scanning for recent changes in files for desktop search");
    } else {
        return i18nc("@info:status", "File indexer is idle.");
    }
}

IndexScheduler::State IndexScheduler::currentStatus() const
{
    return m_state;
}

void IndexScheduler::emitStatusStringChanged()
{
    QString status = userStatusString();
    if (status != m_oldStatus) {
        Q_EMIT statusStringChanged();
        m_oldStatus = status;
    }
}
