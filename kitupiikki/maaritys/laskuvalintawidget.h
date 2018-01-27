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

#ifndef LASKUVALINTAWIDGET_H
#define LASKUVALINTAWIDGET_H

#include "maarityswidget.h"
#include "ui_laskumaaritys.h"

/**
 * @brief Laskutukseen liittyvien valintojen sivu
 */
class LaskuValintaWidget : public MaaritysWidget
{
    Q_OBJECT
public:
    LaskuValintaWidget();
    ~LaskuValintaWidget();

    bool nollaa();
    bool tallenna();
    bool onkoMuokattu();

    QString ohjesivu() override { return "maaritykset/laskutus";}

public slots:
    void ilmoitaMuokattu();

private:
    Ui::LaskuValinnat *ui;
};

#endif // LASKUVALINTAWIDGET_H
