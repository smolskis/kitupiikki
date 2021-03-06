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

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QFileInfo>
#include <QSettings>
#include <QPrinter>
#include <QMessageBox>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QSqlError>
#include <QTextStream>
#include <QBuffer>
#include <QRandomGenerator>

#include <QDebug>

#include <ctime>

#include "kirjanpito.h"
#include "naytin/naytinikkuna.h"

Kirjanpito::Kirjanpito(const QString& portableDir) : QObject(nullptr),
    harjoitusPvm( QDate::currentDate()), tempDir_(nullptr), portableDir_(portableDir)
{
    if( portableDir.isEmpty())
        settings_ = new QSettings(this);
    else
    {
        // Asentamattomassa windows-versiossa asetukset ohjelman hakemistoon
        QDir portable(portableDir);
        settings_ = new QSettings(portable.absoluteFilePath("kitupiikki.ini"),QSettings::IniFormat, this);
    }

    tietokanta_ = QSqlDatabase::addDatabase("QSQLITE");

    asetusModel_ = new AsetusModel(&tietokanta_, this);
    tositelajiModel_ = new TositelajiModel(&tietokanta_, this);
    tiliModel_ = new TiliModel( &tietokanta_, this);
    tilikaudetModel_ = new TilikausiModel(&tietokanta_, this);
    kohdennukset_ = new KohdennusModel(&tietokanta_, this);
    veroTyypit_ = new VerotyyppiModel(this);
    tiliTyypit_ = new TilityyppiModel(this);
    tuotteet_ = new TuoteModel(this);
    liitteet_ = nullptr;

    printer_ = new QPrinter(QPrinter::HighResolution);

    // Jos järjestelmässä ei ole yhtään tulostinta, otetaan käyttöön pdf-tulostus jotte
    // saadaan dialogit

    if( !printer_->isValid())
        printer()->setOutputFileName( QDir::temp().absoluteFilePath("print.pdf") );

    printer_->setPaperSize(QPrinter::A4);
    printer_->setPageMargins(10,5,5,5, QPrinter::Millimeter);
}

Kirjanpito::~Kirjanpito()
{
    tietokanta_.close();
    delete tempDir_;
}

QString Kirjanpito::asetus(const QString &avain) const
{
    return asetukset()->asetus(avain);
}


QDate Kirjanpito::paivamaara() const
{
    if( onkoHarjoitus())
        return harjoitusPvm;
    else
        return QDate::currentDate();
}


Tilikausi Kirjanpito::tilikausiPaivalle(const QDate &paiva) const
{
    return tilikaudet()->tilikausiPaivalle(paiva);
}

TositeModel *Kirjanpito::tositemodel(QObject *parent)
{
    return new TositeModel( &tietokanta_ , parent);
}

void Kirjanpito::ohje(const QString &ohjesivu)
{
    QString osoite("https://kitupiikki.info/");
    osoite.append(ohjesivu);
    if(!QDesktopServices::openUrl( QUrl(osoite)))
        QMessageBox::critical(nullptr, tr("Ohjeen näyttäminen epäonnistui"),
                              tr("Kitupiikki ei saanut käynnistettyä selainta ohjeen näyttämiseksi. Onhan järjestelmässäsi "
                                 "määritelty oletusselain avaamaan internet-sivuja?\n\n"
                                 "Ohjelman ohjeet löytyvät sivulta https://kitupiikki.info"));
}

void Kirjanpito::avaaUrl(const QUrl &url)
{
    if( url.fileName().endsWith(".pdf"))
        NaytinIkkuna::naytaTiedosto( url.path() );
    else if( !QDesktopServices::openUrl(url) )
    {
        if( url.fileName().endsWith("html"))
            QMessageBox::critical(nullptr, tr("Selaimen käynnistäminen epäonnistui"),
                                  tr("Kitupiikki ei saanut käynnistettyä selainta tiedoston %1 näyttämiseksi. Onhan järjestelmässäsi "
                                     "määritelty oletusselain avaamaan internet-sivuja?\n\n").arg(url.toDisplayString()));
        else
            QMessageBox::critical(nullptr, tr("Tiedoston näyttäminen epäonnistui"),
                                  tr("Kitupiikki ei saanut käynnistettyä ulkoista ohjelmaa tiedoston %1 näyttämiseksi.").arg(url.toDisplayString() ));
    }
}

QString Kirjanpito::tilapainen(QString nimi) const
{
    return tempDir_->filePath(nimi.replace("XXXX", satujono(8)));
}

bool Kirjanpito::onkoMaksuperusteinenAlv(const QDate &paiva) const
{
    // Onko annettuna päivänä maksuperusteinen alv käytössä
    if( !asetukset()->onko("AlvVelvollinen") || !asetukset()->onko("MaksuAlvAlkaa"))
        return false;
    if( asetukset()->pvm("MaksuAlvAlkaa") > paiva )
        return false;
    if( asetukset()->onko("MaksuAlvLoppuu") && asetukset()->pvm("MaksuAlvLoppuu") <= paiva )
        return false;
    return true;
}

void Kirjanpito::asetaLogo(const QImage &logo)
{

    logo_ = logo;

    QByteArray ba;

    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);

    logo_.save(&buffer, "PNG");
    buffer.close();

    // Tallennetaan NULL-liitteeksi
    liitteet_->asetaLiite( ba, "logo" );
    liitteet_->tallenna();
}

QString Kirjanpito::arkistopolku() const
{
    if( tiedostopolku().endsWith(".kitupiikki"))
        return tiedostopolku().replace(".kitupiikki",".arkisto");

    QFileInfo info(tiedostopolku());
    return info.dir().absoluteFilePath("arkisto");
}

QString Kirjanpito::viimeVirhe() const
{
    if( virheloki_.isEmpty())
        return QString();
    return virheloki().last();
}

void Kirjanpito::lokiin(const QSqlQuery &kysely)
{
    QString ilmoitus = QString("%1 -> %2")
            .arg(kysely.lastQuery())
            .arg(kysely.lastError().text());

    virheloki_.append(ilmoitus);
    emit tietokantavirhe(ilmoitus);
}



bool Kirjanpito::avaaTietokanta(const QString &tiedosto, bool ilmoitaVirheesta)
{
    tietokanta_.setDatabaseName(tiedosto);
    polkuTiedostoon_ = tiedosto;

    if( tiedosto.isEmpty())
    {
        asetusModel_->tyhjenna();
        emit tietokantaVaihtui();
        return false;
    }

    if( !tietokanta_.open() )
    {
        QMessageBox::critical(nullptr, tr("Tiedostoa %1 ei voi avata").arg(tiedosto),
                              tr("Tiedoston avaamisessa tapahtui virhe\n %1").arg( tietokanta_.lastError().text() ));
        return false;
    }

    // Tehostetaan tietokannan nopeutta määrittelemällä, että tietokanta on vain tämän yhden
    // yhteyden käytössä.

    tietokanta()->exec("PRAGMA LOCKING_MODE = EXCLUSIVE");

    tietokanta()->exec("PRAGMA JOURNAL_MODE = PERSIST");

    if( tietokanta()->lastError().isValid())
    {
        // Tietokanta on jo käytössä
        if( ilmoitaVirheesta )
        {
            if( tietokanta()->lastError().text().contains("locked"))
            {
                QMessageBox::critical(nullptr, tr("Kitupiikki").arg(tiedosto),
                                      tr("Kirjanpitotiedosto on jo käytössä.\n\n%1\n\n"
                                         "Sulje kaikki Kitupiikki-ohjelman ikkunat ja yritä uudelleen.\n"
                                         "Ellei tämä auta, käynnistä tietokoneesi uudelleen.").arg(tiedosto));
            }
            else
            {
                QMessageBox::critical(nullptr, tr("Tiedostoa %1 ei voi avata").arg(tiedosto),
                                  tr("Sql-virhe: %1").arg(tietokanta()->lastError().text()));
            }
        }

        tietokanta()->close();
        asetusModel_->lataa();
        emit tietokantaVaihtui();
        return false;
    }


    // Ladataankin asetukset yms modelista
    asetusModel_->lataa();


    if( asetusModel_->asetus("Nimi").isEmpty() || !asetusModel_->luku("KpVersio"))
    {
        // Tämä ei ole lainkaan kelvollinen tietokanta
        QMessageBox::critical(nullptr, tr("Tiedostoa %1 ei voi avata").arg(tiedosto),
                              tr("Valitsemasi tiedosto ei ole Kitupiikin tietokanta, tai tiedosto on vahingoittunut."));
        tietokanta()->close();
        asetusModel_->lataa();
        emit tietokantaVaihtui();
        return false;
    }


    // Tarkistaa, ettei kirjanpitoa ole tehty uudemmalla versiolla.

    if( asetusModel_->luku("KpVersio") > TIETOKANTAVERSIO )
    {
        // Luotu uudemmalla tietokannalla, sellainen ei kelpaa!
        QMessageBox::critical(nullptr, tr("Kirjanpitoa %1 ei voi avata").arg(asetusModel_->asetus("Nimi")),
                              tr("Kirjanpito on luotu Kitupiikin versiolla %1, eikä käytössäsi oleva versio %2 pysty avaamaan sitä.\n\n"
                                 "Voidaksesi avata tiedoston, sinun on asennettava uudempi versio Kitupiikistä. Lataa ohjelma "
                                 "osoitteesta https://kitupiikki.info").arg( asetusModel_->asetus("LuotuVersiolla"))
                              .arg( qApp->applicationVersion() ));

        tietokanta()->close();
        asetusModel_->lataa();  // Tyhjentää asetukset
        emit tietokantaVaihtui();

        return false;
    }

    //
    // Tiedostoversion muuttuessa tähän muutettava yhteensopivuusversio !!
    //
    if( asetusModel_->luku("KpVersio") < TIETOKANTAVERSIO )
    {
        if( QMessageBox::question(nullptr, tr("Kirjanpidon %1 päivittäminen").arg(asetusModel_->asetus("Nimi")),
                                  tr("Kirjanpito on luotu Kitupiikin versiolla %1 ja se täytyy päivittää, ennen kuin sitä "
                                     "voi käyttää nykyisellä versiolla %2.\n\n"
                                     "Päivittämisen jälkeen kirjanpitoa ei voi enää avata vanhemmilla versioilla kuin 0.11\n\n"
                                     "On erittäin suositeltavaa varmuuskopioida kirjanpito ennen päivittämistä!\n\n"
                                     "Päivitetäänkö tietokanta Kitupiikin nykyiselle versiolle?").arg(asetusModel_->asetus("LuotuVersiolla"))
                                     .arg(qApp->applicationVersion()),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes )
        {
            tietokanta()->close();
            asetusModel_->lataa();
            emit tietokantaVaihtui();
            return false;
        }
        // Tietokanta päivitetään suorittamalla update-komennot
        // nykyiseen tietokantaversioon saakka
        // Esitiedostoversioita 1-2 tuetaan VAIN versioon 0.12 saakka !

        // for(int i = asetusModel_->luku("KpVersio") + 1; i <= TIETOKANTAVERSIO; i++)
        //     paivita( i );

        if( asetusModel_->luku("KpVersio") < 10)
        {
            paivita(3);

            // Erityiset toimet kolmosversioon
            // Liitteiden haku tietokantaan

            QSqlQuery liitekysely("SELECT id, tosite, liiteno FROM liite");
            QSqlQuery liittokysely;
            liittokysely.prepare("UPDATE liite SET data=:data WHERE id=:id" );

            QFileInfo info( kp()->tiedostopolku());

            while( liitekysely.next())
            {
                QString tiedostonnimi = QString("%1-%2.pdf")
                            .arg( liitekysely.value("tosite").toInt()  , 8, 10, QChar('0') )
                            .arg( liitekysely.value("liiteno").toInt() , 2, 10, QChar('0') );

                QFile tiedosto( info.dir().absoluteFilePath("liitteet/" + tiedostonnimi));
                tiedosto.open(QIODevice::ReadOnly);
                liittokysely.bindValue(":data", tiedosto.readAll());
                liittokysely.bindValue(":id", liitekysely.value("id").toInt());
                liittokysely.exec();
            }

            // Laskujen siirtäminen vienteihin

            QSqlQuery laskukysely("SELECT id, tosite, laskupvm, erapvm, avoinSnt, asiakas, kirjausperuste, json FROM lasku");

            while( laskukysely.next())
            {
                int kirjausperuste = laskukysely.value("kirjausperuste").toInt();
                JsonKentta json( laskukysely.value("json").toByteArray());
                QSqlQuery lkysely;

                if( kirjausperuste == LaskuModel::SUORITEPERUSTE || kirjausperuste == LaskuModel::LASKUTUSPERUSTE)
                {
                    lkysely.prepare("UPDATE vienti SET viite=:viite, laskupvm=:laskupvm, erapvm=:erapvm, asiakas=:asiakas, json=:json "
                                    "WHERE id=:era");
                    lkysely.bindValue(":era", json.luku("TaseEra"));
                }
                else if( kirjausperuste == LaskuModel::MAKSUPERUSTE)
                {
                    lkysely.prepare("INSERT INTO vienti(tosite, vientirivi, pvm, kreditsnt, viite, laskupvm, erapvm, asiakas) "
                                    " VALUES(:tosite, 999, :laskupvm, :snt, :viite, :laskupvm, :erapvm, :asiakas");
                    ;
                    lkysely.bindValue(":tosite", laskukysely.value("tosite").toInt());
                    lkysely.bindValue(":snt", laskukysely.value("avoinSnt").toInt());

                }
                else if( kirjausperuste == LaskuModel::KATEISLASKU)
                {
                    lkysely.prepare("UPDATE vienti SET eraid=NULL, viite=:viite, laskupvm=:laskupvm, erapvm=:erapvm, asiakas=:asiakas, json=:json"
                                    "WHERE tosite=:tosite AND debetsnt > 0");
                    lkysely.bindValue(":tosite", laskukysely.value("tosite").toInt());
                }

                lkysely.bindValue(":viite", laskukysely.value("id").toString());
                lkysely.bindValue(":laskupvm", laskukysely.value("laskupvm").toDate());
                lkysely.bindValue(":asiakas", laskukysely.value("asiakas").toString());
                lkysely.bindValue(":erapvm", laskukysely.value("erapvm").toDate());

                json.set("Kirjausperuste", laskukysely.value("kirjausperuste").toInt());
                lkysely.bindValue(":json", json.toJson());

                lkysely.exec();

                if( kirjausperuste == LaskuModel::MAKSUPERUSTE)
                    tietokanta()->exec( QString("UPDATE vienti SET eraid=id WHERE id=%1").arg( lkysely.lastInsertId().toInt() ));
            }
            // Logo
            QFile logotiedosto( info.dir().absoluteFilePath("logo.png") );
            logotiedosto.open(QIODevice::ReadOnly);
            QByteArray ba = logotiedosto.readAll();
            QImage logo = QImage::fromData(ba, "PNG");

            liitteet_ = new LiiteModel(nullptr, this);
            asetaLogo(logo);
            liitteet_->tallenna();
        }

        asetusModel_->aseta("KpVersio", TIETOKANTAVERSIO);
        asetusModel_->aseta("LuotuVersiolla", qApp->applicationVersion());
        QMessageBox::information(nullptr, tr("Kirjanpito päivitetty"),
                                 tr("Kirjanpito päivitetty käytössä olevaan versioon."));

    }
    // Lukitaan tietokanta
    asetusModel_->aseta("Avattu", QDateTime::currentDateTime().toString(Qt::ISODate));

    tositelajiModel_->lataa();
    tiliModel_->lataa();
    tilikaudetModel_->lataa();
    kohdennukset_->lataa();
    tuotteet_->lataa();

    // Tilapäishakemiston luominen
    // #124 Jos väliaikaistiedosto ei toimi...
    delete tempDir_;

    tempDir_ = new QTemporaryDir();

    if( !tempDir_->isValid())
    {
        delete tempDir_;

        QFileInfo info( kp()->tiedostopolku() );

        tempDir_ = new QTemporaryDir( info.dir().absoluteFilePath("Temp")  );
        if( !tempDir_->isValid())
            QMessageBox::critical(nullptr, tr("Tilapäishakemiston luominen epäonnistui"),
                                  tr("Kitupiikki ei onnistunut luomaan tilapäishakemistoa. Raporttien ja laskujen esikatselu ei toimi."));
    }

    // Ladataan logo    
    liitteet_ = new LiiteModel(nullptr, this);
    logo_ = QImage::fromData( liitteet_->liite("logo") , "PNG" );


    // Ilmoitetaan, että tietokanta on vaihtunut
    emit tietokantaVaihtui();

    return true;
}

bool Kirjanpito::lataaUudelleen()
{
    return avaaTietokanta(tiedostopolku());
}

void Kirjanpito::asetaHarjoitteluPvm(const QDate &pvm)
{
    harjoitusPvm = pvm;
}


Kirjanpito *Kirjanpito::db()
{
    return instanssi__;
}

void Kirjanpito::asetaInstanssi(Kirjanpito *kp)
{
    instanssi__ = kp;
}


QString Kirjanpito::satujono(int pituus)
{
    // https://stackoverflow.com/questions/18862963/qt-c-random-string-generation
    const QString merkit("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");

    QString randomString;
    for(int i=0; i<pituus; ++i)
    {
       int index = QRandomGenerator::global()->generate() % merkit.length() ;
       QChar nextChar = merkit.at(index);
       randomString.append(nextChar);
    }
    return randomString;
}

void Kirjanpito::paivita(int versioon)
{
    QFile sqltiedosto( QString(":/sql/update%1.sql").arg(versioon));
    sqltiedosto.open(QIODevice::ReadOnly);
    QTextStream in(&sqltiedosto);
    QString sqluonti = in.readAll();
    sqluonti.replace("\n"," ");
    QStringList sqlista = sqluonti.split(";");
    QSqlQuery query;

    foreach (QString kysely,sqlista)
    {
        query.exec(kysely);
        qApp->processEvents();
    }
}

Kirjanpito* Kirjanpito::instanssi__ = nullptr;

Kirjanpito *kp()  { return Kirjanpito::db(); }
