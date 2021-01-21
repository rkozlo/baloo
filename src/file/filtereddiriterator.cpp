/*
    SPDX-FileCopyrightText: 2014 Vishesh Handa <me@vhanda.in>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "filtereddiriterator.h"
#include "fileindexerconfig.h"


using namespace Baloo;

FilteredDirIterator::FilteredDirIterator(const FileIndexerConfig* config, const QString& folder, Filter filter)
    : m_config(config)
    , m_currentIter(nullptr)
    , m_filters(QDir::NoDotAndDotDot | QDir::Readable | QDir::NoSymLinks | QDir::Hidden)
    , m_firstItem(false)
{
    if (filter == DirsOnly) {
        m_filters |= QDir::Dirs;
    } else if (filter == FilesAndDirs) {
        m_filters |= (QDir::Files | QDir::Dirs);
    }

    if (!m_config || m_config->shouldFolderBeIndexed(folder)) {
        m_currentIter = new QDirIterator(folder, m_filters);
        m_firstItem = true;
    }
}

FilteredDirIterator::~FilteredDirIterator()
{
    delete m_currentIter;
}

QString FilteredDirIterator::next()
{
    if (m_firstItem) {
        m_firstItem = false;
        m_filePath = m_currentIter->path();
        m_fileInfo = QFileInfo(m_filePath);
        return m_filePath;
    }

    m_filePath.clear();
    if (!m_currentIter) {
        m_fileInfo = QFileInfo();
        return QString();
    }

    bool shouldIndexHidden = false;
    if (m_config)
        shouldIndexHidden = m_config->indexHiddenFilesAndFolders();

    while (true) {
        // Last entry in the current directory found, try the next
        // directory from the stack
        // Loop until the directory is non-empty, or the stack is empty
        while (!m_currentIter->hasNext()) {
            delete m_currentIter;
            m_currentIter = nullptr;

            if (m_paths.isEmpty()) {
                m_fileInfo = QFileInfo();
                return QString();
            }

            const QString path = m_paths.pop();
            m_currentIter = new QDirIterator(path, m_filters);
        }

        m_filePath = m_currentIter->next();
        m_fileInfo = m_currentIter->fileInfo();

        if (m_fileInfo.isDir()) {
            if (shouldIndexFolder(m_filePath)) {
                // Push the current item to the directory stack
                m_paths.push(m_filePath);
                return m_filePath;
            }
        }
        else if (m_fileInfo.isFile()) {
            bool shouldIndexFile = (shouldIndexHidden || !m_fileInfo.isHidden())
                                   && (!m_config || m_config->shouldFileBeIndexed(m_fileInfo.fileName()));
            if (shouldIndexFile) {
                return m_filePath;
            }
        }
    }
}

QString FilteredDirIterator::filePath() const
{
    return m_filePath;
}

QFileInfo FilteredDirIterator::fileInfo() const
{
    return m_fileInfo;
}

bool FilteredDirIterator::shouldIndexFolder(const QString& path) const
{
    if (!m_config) {
        return !QFileInfo(path).isHidden();
    }

    QString folder;
    if (m_config->folderInFolderList(path, folder)) {
        // we always index the folders in the list
        // ignoring the name filters
        if ((folder == path) || (folder == QString(path).append('/')))
            return true;

        // check for hidden folders
        QFileInfo fi(path);
        if (!m_config->indexHiddenFilesAndFolders() && fi.isHidden())
            return false;

        return m_config->shouldFileBeIndexed(fi.fileName());
    }

    return false;
}

