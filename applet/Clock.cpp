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

#include "Clock.h"
#include "DataSource.h"

#include <QtCore/QFile>

#include <KDateTime>
#include <KCalendarSystem>
#include <KSystemTimeZones>

#include <Plasma/Theme>
#include <Plasma/ToolTipManager>

namespace AdjustableClock
{

Clock::Clock(DataSource *parent, ClockMode mode) : QObject(parent),
    m_source(parent),
    m_document(NULL),
    m_mode(mode)
{
    if (m_mode == StandardClock) {
        connect(Plasma::Theme::defaultTheme(), SIGNAL(themeChanged()), this, SLOT(updateTheme()));
        connect(m_source, SIGNAL(dataChanged(QList<ClockTimeValue>)), this, SLOT(updateClock(QList<ClockTimeValue>)));
    }

    QFile file(":/clock.js");
    file.open(QIODevice::ReadOnly | QIODevice::Text);

    m_engine.globalObject().setProperty("Clock", m_engine.newQObject(this), QScriptValue::Undeletable);
    m_engine.evaluate(QString(file.readAll()));
}

void Clock::exposeClock()
{
    if (m_document) {
        QFile file(":/clock.js");
        file.open(QIODevice::ReadOnly | QIODevice::Text);

        m_document->addToJavaScriptWindowObject("Clock", this);
        m_document->evaluateJavaScript(QString(file.readAll()));
    }
}

void Clock::updateClock(const QList<ClockTimeValue> &changes)
{
    for (int i = 0; i < changes.count(); ++i) {
        if (m_document) {
            m_document->evaluateJavaScript(QString("var event = document.createEvent('Event'); event.initEvent('Clock%1Changed', false, false); document.dispatchEvent(event);").arg(getComponentString(changes.at(i))));
        }

        if (m_rules.contains(changes.at(i))) {
            for (int j = 0; j < m_rules[changes.at(i)].count(); ++j) {
                applyRule(m_rules[changes.at(i)].at(j));
            }
        }
    }
}

void Clock::updateTheme()
{
    if (m_document) {
        m_document->evaluateJavaScript("var event = document.createEvent('Event'); event.initEvent('ClockThemeChanged', false, false); document.dispatchEvent(event);");
    }
}

void Clock::applyRule(const Placeholder &rule)
{
    if (m_document) {
        setValue(m_document->findAllElements(rule.rule), rule.attribute, toString(rule.value, rule.options));
    }
}

void Clock::setDocument(QWebFrame *document)
{
    m_rules.clear();

    m_document = document;

    exposeClock();

    connect(m_document, SIGNAL(javaScriptWindowObjectCleared()), this, SLOT(exposeClock()));
}

void Clock::setRule(const QString &rule, const QString &attribute, int value, const QVariantMap &options)
{
    const ClockTimeValue nativeValue  = static_cast<ClockTimeValue>(value);

    if (m_mode == EditorClock && attribute.isEmpty() && m_document) {
        QString title;

        switch (nativeValue) {
        case SecondValue:
            title = i18n("Second");

            break;
        case MinuteValue:
            title = i18n("Minute");

            break;
        case HourValue:
            title = i18n("Hour");

            break;
        case TimeOfDayValue:
            title = i18n("The pm or am string");

            break;
        case DayOfWeekValue:
            title = i18n("Weekday");

            break;
        case DayOfMonthValue:
            title = i18n("Day of the month");

            break;
        case DayOfYearValue:
            title = i18n("Day of the year");

            break;
        case WeekValue:
            title = i18n("Week");

            break;
        case MonthValue:
            title = i18n("Month");

            break;
        case YearValue:
            title = i18n("Year");

            break;
        case TimestampValue:
            title = i18n("UNIX timestamp");

            break;
        case TimeValue:
            title = i18n("Time");

            break;
        case DateValue:
            title = i18n("Date");

            break;
        case DateTimeValue:
            title = i18n("Date and time");

            break;
        case TimeZoneNameValue:
            title = i18n("Timezone name");

            break;
        case TimeZoneAbbreviationValue:
            title = i18n("Timezone abbreviation");

            break;
        case TimeZoneOffsetValue:
            title = i18n("Timezone offset");

            break;
        case TimeZonesValue:
            title = i18n("Timezones list");

            break;
        case HolidaysValue:
            title = i18n("Holidays list");

            break;
        case EventsValue:
            title = i18n("Events list");

            break;
        case SunriseValue:
            title = i18n("Sunrise time");

            break;
        case SunsetValue:
            title = i18n("Sunset time");

            break;
        default:
            title = QString();

            break;
        }

        const QWebElementCollection elements = m_document->findAllElements(rule);

        for (int i = 0; i < elements.count(); ++i) {
            elements.at(i).setInnerXml(QString("<placeholder title=\"%1\"><fix> </fix>%2<fix> </fix></placeholder>").arg(title).arg(m_source->toString(nativeValue, options, QDateTime(QDate(2000, 1, 1), QTime(12, 30, 15)))));
        }

        return;
    }

    Placeholder placeholder;
    placeholder.rule = rule;
    placeholder.attribute = attribute;
    placeholder.value = nativeValue;
    placeholder.options = options;

    if (m_mode == StandardClock) {
        if (!m_rules.contains(nativeValue)) {
            m_rules[nativeValue] = QList<Placeholder>();
        }

        m_rules[nativeValue].append(placeholder);
    }

    applyRule(placeholder);
}

void Clock::setRule(const QString &rule, int value, const QVariantMap &options)
{
    setRule(rule, QString(), value, options);
}

void Clock::setValue(const QString &rule, const QString &attribute, const QString &value)
{
    if (!m_document) {
        return;
    }

    const QWebElementCollection elements = m_document->findAllElements(rule);

    for (int i = 0; i < elements.count(); ++i) {
        if (attribute.isEmpty()) {
            elements.at(i).setInnerXml(value);
        } else {
            elements.at(i).setAttribute(attribute, value);
        }
    }
}

void Clock::setValue(const QString &rule, const QString &value)
{
    setValue(rule, QString(), value);
}

void Clock::setValue(const QWebElementCollection &elements, const QString &attribute, const QString &value)
{
    for (int i = 0; i < elements.count(); ++i) {
        if (attribute.isEmpty()) {
            elements.at(i).setInnerXml(value);
        } else {
            elements.at(i).setAttribute(attribute, value);
        }
    }
}

QString Clock::evaluate(const QString &script)
{
    return m_engine.evaluate(script).toString();
}

QString Clock::toString(int value, const QVariantMap &options) const
{
    return m_source->toString(static_cast<ClockTimeValue>(value), options, ((m_mode == StandardClock) ? QDateTime() : QDateTime(QDate(2000, 1, 1), QTime(12, 30, 15))));
}

QString Clock::getComponentString(ClockTimeValue value)
{
    switch (value) {
    case SecondValue:
        return "Second";
    case MinuteValue:
        return "Minute";
    case HourValue:
        return "Hour";
    case TimeOfDayValue:
        return "TimeOfDay";
    case DayOfMonthValue:
        return "DayOfMonth";
    case DayOfWeekValue:
        return "DayOfWeek";
    case DayOfYearValue:
        return "DayOfYear";
    case WeekValue:
        return "Week";
    case MonthValue:
        return "Month";
    case YearValue:
        return "Year";
    case TimestampValue:
        return "Timestamp";
    case TimeValue:
        return "Time";
    case DateValue:
        return "Date";
    case DateTimeValue:
        return "DateTime";
    case TimeZoneNameValue:
        return "TimeZoneName";
    case TimeZoneAbbreviationValue:
        return "TimeZoneAbbreviation";
    case TimeZoneOffsetValue:
        return "TimeZoneOffset";
    case TimeZonesValue:
        return "TimeZones";
    case EventsValue:
        return "Events";
    case HolidaysValue:
        return "Holidays";
    case SunriseValue:
        return "Sunrise";
    case SunsetValue:
        return "Sunset";
    default:
        return QString();
    }

    return QString();
}

}
