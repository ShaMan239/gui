// Copyright (c) 2011-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/bantablemodel.h>

#include <interfaces/node.h>
#include <net_types.h> // For banmap_t
#include <qt/guiconstants.h>

#include <utility>

#include <QDateTime>
#include <QList>
#include <QLocale>
#include <QModelIndex>
#include <QString>
#include <QTimer>
#include <QVariant>

bool BannedNodeLessThan::operator()(const CCombinedBan& left, const CCombinedBan& right) const
{
    const CCombinedBan* pLeft = &left;
    const CCombinedBan* pRight = &right;

    if (order == Qt::DescendingOrder)
        std::swap(pLeft, pRight);

    switch (static_cast<BanTableModel::ColumnIndex>(column)) {
    case BanTableModel::Address:
        return pLeft->subnet.ToString().compare(pRight->subnet.ToString()) < 0;
    case BanTableModel::Bancreate:
        return pLeft->banEntry.nCreateTime < pRight->banEntry.nCreateTime;
    case BanTableModel::Bantime:
        return pLeft->banEntry.nBanUntil < pRight->banEntry.nBanUntil;
    case BanTableModel::Bandur:
        return (pLeft->banEntry.nBanUntil - pLeft->banEntry.nCreateTime) < (pRight->banEntry.nBanUntil - pRight->banEntry.nCreateTime);
    case BanTableModel::remain:
        return pLeft->banEntry.nBanUntil < pRight->banEntry.nBanUntil;
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

// private implementation
class BanTablePriv
{
public:
    /** Local cache of peer information */
    QList<CCombinedBan> cachedBanlist;
    /** Column to sort nodes by (default to unsorted) */
    int sortColumn{-1};
    /** Order (ascending or descending) to sort nodes by */
    Qt::SortOrder sortOrder;

    /** Pull a full list of banned nodes from CNode into our cache */
    void refreshBanlist(interfaces::Node& node)
    {
        banmap_t banMap;
        node.getBanned(banMap);

        cachedBanlist.clear();
        cachedBanlist.reserve(banMap.size());
        for (const auto& entry : banMap)
        {
            CCombinedBan banEntry;
            banEntry.subnet = entry.first;
            banEntry.banEntry = entry.second;
            cachedBanlist.append(banEntry);
        }

        if (sortColumn >= 0)
            // sort cachedBanlist (use stable sort to prevent rows jumping around unnecessarily)
            std::stable_sort(cachedBanlist.begin(), cachedBanlist.end(), BannedNodeLessThan(sortColumn, sortOrder));
    }

    int size() const
    {
        return cachedBanlist.size();
    }

    CCombinedBan *index(int idx)
    {
        if (idx >= 0 && idx < cachedBanlist.size())
            return &cachedBanlist[idx];

        return nullptr;
    }

    /** Function to add the functionality of displaying only two hightest degree
        of non-zero time value */
    QString remainingTime(qint64 seconds)
    {
        const qint64 arr1[] = {31536000, 2592000, 86400, 3600, 60, 1};
        const QString arr2[] = {"Year", "Month", "Day", "Hour", "Minute", "Second"};
        const QString arr3[] = {"Years", "Months", "Days", "Hours", "Minutes", "Seconds"};
        int n_terms = 6;
        QString result = QString();
        int degree = 0;

        for (int i = 0; i < n_terms; i++) {
            if (degree > 1) {
                break;
            }
            qint64 temp = seconds / arr1[i];
            if (temp > 0) {
                if (temp == 1)
                    result += QString::fromStdString(" ") + QString::number(temp) + QString::fromStdString(" ") + arr2[i] + QString::fromStdString(",");
                else
                    result += QString::fromStdString(" ") + QString::number(temp) + QString::fromStdString(" ") + arr3[i] + QString::fromStdString(",");
                degree++;
            }
            seconds %= arr1[i];
        }
        if(!result.isEmpty())
            result.chop(1);
        return result;
    }
};

BanTableModel::BanTableModel(interfaces::Node& node, QObject* parent) :
    QAbstractTableModel(parent),
    m_node(node),
    timer(nullptr)
{
    // set up timer for auto refresh
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &BanTableModel::refresh);
    timer->setInterval(MODEL_UPDATE_DELAY);

    columns << tr("IP/Netmask") << tr("Ban Created") << tr("Banned Until") << tr("Duration") << tr("Time Remaining");
    priv.reset(new BanTablePriv());

    // load initial data
    refresh();
}

BanTableModel::~BanTableModel()
{
    // Intentionally left empty
}

void BanTableModel::startAutoRefresh()
{
    timer->start();
}

void BanTableModel::stopAutoRefresh()
{
    timer->stop();
}


int BanTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return priv->size();
}

int BanTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return columns.length();
}


QVariant BanTableModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();

    CCombinedBan *rec = static_cast<CCombinedBan*>(index.internalPointer());

    const auto column = static_cast<ColumnIndex>(index.column());
    if (role == Qt::DisplayRole) {
        switch (column) {
        case Address:
            return QString::fromStdString(rec->subnet.ToString());
        case Bancreate: {
            QDateTime date = QDateTime::fromMSecsSinceEpoch(0);
            date = date.addSecs(rec->banEntry.nCreateTime);
            return QLocale::system().toString(date, QLocale::LongFormat);
        }
        case Bantime: {
            QDateTime date = QDateTime::fromMSecsSinceEpoch(0);
            date = date.addSecs(rec->banEntry.nBanUntil);
            return QLocale::system().toString(date, QLocale::LongFormat);
        }
        case Bandur: {
            QDateTime date = QDateTime::fromMSecsSinceEpoch(0);
            date = date.addSecs(rec->banEntry.nBanUntil - rec->banEntry.nCreateTime);
            return priv->remainingTime(date.toSecsSinceEpoch());
        }
        case remain: {
            const int64_t current_time{GetTimeSeconds()};
            QDateTime date = QDateTime::fromMSecsSinceEpoch(0);
            date = date.addSecs(rec->banEntry.nBanUntil - current_time);
            return priv->remainingTime(date.toSecsSinceEpoch());
        }
        } // no default case, so the compiler can warn about missing cases
        assert(false);
    } else if (role == Qt::TextAlignmentRole) {
        switch (column) {
        case Address:
        case Bancreate:
        case Bantime:
        case Bandur:
            return QVariant(Qt::AlignCenter);
        case remain:
            return QVariant(Qt::AlignCenter);
        }
    }

    return QVariant();
}


QVariant BanTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation == Qt::Horizontal)
    {
        if(role == Qt::DisplayRole && section < columns.size())
        {
            return columns[section];
        }
    }
    return QVariant();
}

Qt::ItemFlags BanTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;

    Qt::ItemFlags retval = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    return retval;
}

QModelIndex BanTableModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    CCombinedBan *data = priv->index(row);

    if (data)
        return createIndex(row, column, data);
    return QModelIndex();
}

void BanTableModel::refresh()
{
    Q_EMIT layoutAboutToBeChanged();
    priv->refreshBanlist(m_node);
    Q_EMIT layoutChanged();
}

void BanTableModel::sort(int column, Qt::SortOrder order)
{
    priv->sortColumn = column;
    priv->sortOrder = order;
    refresh();
}

bool BanTableModel::shouldShow()
{
    return priv->size() > 0;
}
