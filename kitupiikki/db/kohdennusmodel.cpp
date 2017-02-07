/*
   Copyright (C) 2017 Arto Hyvättinen

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <QSqlQuery>

#include "kohdennusmodel.h"
#include "db/kirjanpito.h"
#include "db/tilikausi.h"




KohdennusModel::KohdennusModel(QObject *parent) :
    QAbstractTableModel(parent)
{

}

int KohdennusModel::rowCount(const QModelIndex & /* parent */) const
{
    return projektit_.count();
}

int KohdennusModel::columnCount(const QModelIndex & /* parent */) const
{
    return 3;
}

QVariant KohdennusModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if( role == Qt::TextAlignmentRole)
        return QVariant( Qt::AlignCenter | Qt::AlignVCenter);
    else if( role != Qt::DisplayRole )
        return QVariant();
    else if( orientation == Qt::Horizontal)
    {
        switch (section)
        {
            case NIMI:
                return QVariant("Nimi");
            case ALKAA :
                return QVariant("Alkaa");
            case PAATTYY:
                return QVariant("Päättyy");
        }

    }
    return QVariant( section + 1);
}

QVariant KohdennusModel::data(const QModelIndex &index, int role) const
{
    if( !index.isValid())
        return QVariant();

    Kohdennus projekti = projektit_[index.row()];

    if( role == IdRooli)
        return QVariant( projekti.id() );
    else if( role == Qt::TextAlignmentRole)
        return QVariant( Qt::AlignLeft | Qt::AlignVCenter);
    else if( role == Qt::DisplayRole || role == Qt::EditRole)
    {
        switch( index.column())
        {
            case NIMI: return QVariant( projekti.nimi() );
            case ALKAA: return QVariant(projekti.alkaa() );
            case PAATTYY: return QVariant(projekti.paattyy());
        }
    }
    return QVariant();
}

Qt::ItemFlags KohdennusModel::flags(const QModelIndex &index) const
{
    return QAbstractTableModel::flags(index) | Qt::ItemIsEditable;
}

bool KohdennusModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if( role != Qt::EditRole)
        return false;

    if( index.column() == NIMI)
        projektit_[ index.row() ].asetaNimi(value.toString());
    else if( index.column() == ALKAA )
        projektit_[ index.row() ].asetaAlkaa( value.toDate() );
    else if( index.column() == PAATTYY)
        projektit_[ index.column()].asetaPaattyy( value.toDate() );

    return true;
}

QString KohdennusModel::nimi(int id) const
{
    return projekti(id).nimi();
}

Kohdennus KohdennusModel::projekti(int id) const
{
    foreach (Kohdennus projekti, projektit_)
    {
        if( projekti.id() == id)
            return projekti;
    }
    return Kohdennus();
}

QList<Kohdennus> KohdennusModel::projektit() const
{
    return projektit_;
}

void KohdennusModel::lataa()
{
    beginResetModel();
    projektit_.clear();
    QSqlQuery kysely("select id, nimi, alkaa, loppuu FROM projekti");
    while( kysely.next() )
    {
        projektit_.append( Kohdennus( kysely.value(0).toInt(),
                                     kysely.value(1).toString(),
                                     kysely.value(2).toDate(),
                                     kysely.value(3).toDate()));
    }
    endResetModel();
}

void KohdennusModel::lisaaUusi(const QString nimi)
{
    beginInsertRows(QModelIndex(), projektit_.count(), projektit_.count());

    // Oletuksena projekti on nykyisen tilikauden pituinen
    Tilikausi nykyinen = Kirjanpito::db()->tilikausiPaivalle( Kirjanpito::db()->paivamaara() );
    projektit_.append( Kohdennus(0, nimi, nykyinen.alkaa(), nykyinen.paattyy()) );

    endInsertRows();
}


