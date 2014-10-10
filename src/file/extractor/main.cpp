/*
   This file is part of the Nepomuk KDE project.
   Copyright (C) 2010-14 Vishesh Handa <handa.vish@gmail.com>
   Copyright (C) 2010-2011 Sebastian Trueg <trueg@kde.org>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) version 3, or any
   later version accepted by the membership of KDE e.V. (or its
   successor approved by the membership of KDE e.V.), which shall
   act as a proxy defined in Section 6 of version 3 of the license.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "app.h"
#include "../priority.h"

#include <KLocalizedString>
#include <QStandardPaths>

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>

int main(int argc, char* argv[])
{
    lowerIOPriority();
    setIdleSchedulingPriority();
    lowerPriority();

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QLatin1String("Baloo File Extractor"));
    QCoreApplication::setApplicationVersion(QLatin1String("0.1"));

    QCommandLineParser parser;
    parser.setApplicationDescription(i18n("The File Extractor extracts the file metadata and text"));
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addPositionalArgument(QLatin1String("urls"), i18n("The URL/id of the files to be indexed"));
    parser.addOption(QCommandLineOption(QLatin1String("debug"), i18n("Print the data being indexed")));
    parser.addOption(QCommandLineOption(QLatin1String("ignoreConfig"), i18n("Ignore the baloofilerc config and always index the file")));

    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1String("/baloo/file");
    parser.addOption(QCommandLineOption(QLatin1String("db"), i18n("Specify a custom path for the database"),
                                        i18n("path"), path));

    parser.process(app);

    QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        fprintf(stderr, "The url/id of the file is missing\n\n");
        parser.showHelp(1);
    }

    Baloo::App appObject(parser.value(QLatin1String("db")));
    appObject.setDebug(parser.isSet(QLatin1String("debug")));
    appObject.setIgnoreConfig(parser.isSet(QLatin1String("ignoreConfig")));
    appObject.startProcessing(args);

    return app.exec();
}
