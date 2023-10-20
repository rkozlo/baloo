/*
    This file is part of the KDE Baloo project.
    SPDX-FileCopyrightText: 2015 Vishesh Handa <vhanda@kde.org>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "orpostingiterator.h"
#include "vectorpostingiterator.h"

#include <QTest>

using namespace Baloo;

class OrPostingIteratorTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void test();
    void testNullIterators();
};

void OrPostingIteratorTest::test()
{
    QList<quint64> l1 = {1, 3, 5, 7};
    QList<quint64> l2 = {3, 4, 5, 7, 9, 11};
    QList<quint64> l3 = {1, 3, 7};

    VectorPostingIterator* it1 = new VectorPostingIterator(l1);
    VectorPostingIterator* it2 = new VectorPostingIterator(l2);
    VectorPostingIterator* it3 = new VectorPostingIterator(l3);

    QList<PostingIterator *> vec = {it1, it2, it3};
    OrPostingIterator it(vec);
    QCOMPARE(it.docId(), static_cast<quint64>(0));

    QList<quint64> result = {1, 3, 4, 5, 7, 9, 11};
    for (quint64 val : result) {
        QCOMPARE(it.next(), static_cast<quint64>(val));
        QCOMPARE(it.docId(), static_cast<quint64>(val));
    }
    QCOMPARE(it.next(), static_cast<quint64>(0));
    QCOMPARE(it.docId(), static_cast<quint64>(0));
}

void OrPostingIteratorTest::testNullIterators()
{
    QList<quint64> l1 = {1, 3, 5, 7};
    QList<quint64> l2 = {3, 4, 5, 7, 9, 11};
    QList<quint64> l3 = {1, 3, 7};

    VectorPostingIterator* it1 = new VectorPostingIterator(l1);
    VectorPostingIterator* it2 = new VectorPostingIterator(l2);
    VectorPostingIterator* it3 = new VectorPostingIterator(l3);

    QList<PostingIterator *> vec = {it1, nullptr, it2, nullptr, it3};
    OrPostingIterator it(vec);
    QCOMPARE(it.docId(), static_cast<quint64>(0));

    QList<quint64> result = {1, 3, 4, 5, 7, 9, 11};
    for (quint64 val : result) {
        QCOMPARE(it.next(), static_cast<quint64>(val));
        QCOMPARE(it.docId(), static_cast<quint64>(val));
    }
    QCOMPARE(it.next(), static_cast<quint64>(0));
    QCOMPARE(it.docId(), static_cast<quint64>(0));
}


QTEST_MAIN(OrPostingIteratorTest)

#include "orpostingiteratortest.moc"
