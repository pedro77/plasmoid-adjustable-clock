/***********************************************************************************
* Adjustable Clock: Plasmoid to show date and time in adjustable format.
* Copyright (C) 2008 - 2013 Michal Dutkiewicz aka Emdek <emdeck@gmail.com>
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*
***********************************************************************************/

#ifndef ADJUSTABLECLOCKAPPLET_HEADER
#define ADJUSTABLECLOCKAPPLET_HEADER

#include <QtCore/QList>
#include <QtCore/QDateTime>
#include <QtWebKit/QWebPage>
#include <QtScript/QScriptEngine>

#include <Plasma/Applet>
#include <Plasma/DataEngine>

#include <plasmaclock/clockapplet.h>

namespace AdjustableClock
{

enum ClockFeature
{
    NoFeatures = 0,
    SecondsClockFeature = 1,
    SecondsToolTipFeature = 2,
    HolidaysFeature = 4,
    EventsFeature = 8
};

Q_DECLARE_FLAGS(ClockFeatures, ClockFeature)

struct Theme
{
    QString id;
    QString title;
    QString description;
    QString author;
    QString html;
    QString css;
    QString script;
    bool background;
    bool bundled;
};

class Applet : public ClockApplet
{
    Q_OBJECT

    public:
        Applet(QObject *parent, const QVariantList &args);

        void init();
        void saveCustomThemes(const QList<Theme> &themes);
        QDateTime currentDateTime() const;
        QStringList clipboardFormats() const;
        QList<Theme> themes() const;
        Theme theme() const;
        QString evaluateFormat(const QString &format, QDateTime dateTime = QDateTime(), bool special = false);
        QString evaluatePlaceholder(ushort placeholder, QDateTime dateTime, int alternativeForm, bool shortForm, bool textualForm);
        QString evaluatePlaceholder(ushort placeholder, int alternativeForm, bool shortForm, bool textualForm);
        static QString pageLayout(const QString &html, const QString &css, const QString &script, const QString &head = QString());

    protected:
        void constraintsEvent(Plasma::Constraints constraints);
        void resizeEvent(QGraphicsSceneResizeEvent *event);
        void mousePressEvent(QGraphicsSceneMouseEvent *event);
        void paintInterface(QPainter *painter, const QStyleOptionGraphicsItem *option, const QRect &contentsRect);
        void createClockConfigurationInterface(KConfigDialog *parent);
        void changeEngineTimezone(const QString &oldTimezone, const QString &newTimezone);
        void connectSource(const QString &timezone);
        void setTheme(const QString &html, const QString &css, const QString &script);
        void updateEvents();
        void updateHolidays();
        static QString extractExpression(const QString &format, int &i);
        static QString extractNumber(const QString &format, int &i);
        static QString formatNumber(int number, int length);
        QPair<QString, QString> toolTipFormat() const;
        QList<Theme> loadThemes(const QString &path, bool bundled) const;
        QList<QAction*> contextualActions();

    protected slots:
        void dataUpdated(const QString &name, const Plasma::DataEngine::Data &data, bool force = false);
        void clockConfigChanged();
        void clockConfigAccepted();
        void copyToClipboard();
        void toolTipAboutToShow();
        void toolTipHidden();
        void copyToClipboard(QAction *action);
        void updateClipboardMenu();
        void updateToolTipContent();
        void updateSize();
        void updateTheme();
        void repaint();

    private:
        QWebPage m_page;
        QScriptEngine m_engine;
        QStringList m_holidays;
        QStringList m_timezoneArea;
        QString m_currentHtml;
        QString m_timezoneAbbreviation;
        QString m_timezoneOffset;
        QString m_eventsShort;
        QString m_eventsLong;
        QString m_eventsQuery;
        QDateTime m_dateTime;
        QTime m_sunrise;
        QTime m_sunset;
        QFlags<ClockFeature> m_features;
        QAction *m_clipboardAction;
        QList<Theme> m_themes;
        int m_theme;
};

}

#endif
