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
#include "Applet.h"

#include <KDateTime>
#include <KCalendarSystem>
#include <KSystemTimeZones>

#include <Plasma/Theme>
#include <Plasma/ToolTipManager>

namespace AdjustableClock
{

Clock::Clock(Applet *parent) : QObject(parent),
    m_applet(parent),
    m_document(NULL)
{
    connect(Plasma::Theme::defaultTheme(), SIGNAL(themeChanged()), this, SIGNAL(themeChanged()));
}

void Clock::reset()
{
    QRegExp formatWithSeconds = QRegExp("%[\\~\\d\\!\\$\\:\\+\\-]*[ast]");
    QFlags<ClockFeature> features;
    const Theme theme = m_applet->getTheme();
    const QPair<QString, QString> toolTipFormat = m_applet->getToolTipFormat();
    const QString toolTip = (toolTipFormat.first + QChar('|') + toolTipFormat.second);
    const QString string = (theme.html + QChar('|') + toolTip);

    if (theme.html.contains(formatWithSeconds)) {
        features |= SecondsClockFeature;
    }

    if (toolTip.contains(formatWithSeconds)) {
        features |= SecondsToolTipFeature;
    }

    if (string.contains(QRegExp("%[\\d\\!\\$\\:\\+\\-]*H"))) {
        features |= HolidaysFeature;
    }

    if (string.contains(QRegExp("%[\\d\\!\\$\\:\\+\\-]*E"))) {
        features |= EventsFeature;

        if (m_eventsQuery.isEmpty()) {
            m_eventsQuery = QString("events:%1:%2").arg(QDate::currentDate().toString(Qt::ISODate)).arg(QDate::currentDate().addDays(1).toString(Qt::ISODate));

            m_applet->dataEngine("calendar")->connectSource(m_eventsQuery, this);
        }
    } else if (!m_eventsQuery.isEmpty()) {
        m_applet->dataEngine("calendar")->disconnectSource(m_eventsQuery, this);

        m_eventsQuery = QString();
    }

    m_features = features;

    if (m_document) {
        m_document->addToJavaScriptWindowObject("Clock", this, QScriptEngine::QtOwnership);
    }

    m_rules.clear();
}

void Clock::dataUpdated(const QString &source, const Plasma::DataEngine::Data &data, bool force)
{
    if (!source.isEmpty() && source == m_eventsQuery) {
        updateEvents();

        return;
    }

    m_dateTime = QDateTime(data["Date"].toDate(), data["Time"].toTime());

    const int second = ((m_features & SecondsClockFeature || m_features & SecondsToolTipFeature) ? m_dateTime.time().second() : 0);

    if (m_features & HolidaysFeature && (force || (m_dateTime.time().hour() == 0 && m_dateTime.time().minute() == 0 && second == 0))) {
        updateHolidays();
    }

    if (m_features & EventsFeature && QTime::currentTime().hour() == 0 && m_dateTime.time().minute() == 0 && second == 0) {
        m_applet->dataEngine("calendar")->connectSource(m_eventsQuery, this);

        m_eventsQuery = QString("events:%1:%2").arg(QDate::currentDate().toString(Qt::ISODate)).arg(QDate::currentDate().addDays(1).toString(Qt::ISODate));

        m_applet->dataEngine("calendar")->connectSource(m_eventsQuery, this);
    }

    if (force || (m_dateTime.time().minute() == 0 && second == 0)) {
        Plasma::DataEngine::Data sunData = m_applet->dataEngine("time")->query(QString("%1|Solar").arg(m_applet->currentTimezone()));

        m_sunrise = sunData["Sunrise"].toDateTime().time();
        m_sunset = sunData["Sunset"].toDateTime().time();
    }

    if (force || m_features & SecondsClockFeature || second == 0) {
        const Theme theme = m_applet->getTheme();

        m_applet->setTheme(evaluateFormat(theme.html, m_dateTime), theme.css, theme.script);
    }

    if (Plasma::ToolTipManager::self()->isVisible(m_applet) && (force || m_features & SecondsToolTipFeature || second == 0)) {
        m_applet->updateToolTipContent();
    }
}

void Clock::connectSource(const QString &timezone)
{
    reset();

    const bool alignToSeconds = (m_features & SecondsClockFeature || m_features & SecondsToolTipFeature);

    m_applet->dataEngine("time")->connectSource(timezone, this, (alignToSeconds ? 1000 : 60000), (alignToSeconds ? Plasma::NoAlignment : Plasma::AlignToMinute));

    const KTimeZone timezoneData = (m_applet->isLocalTimezone() ? KSystemTimeZones::local() : KSystemTimeZones::zone(m_applet->currentTimezone()));

    m_timezoneAbbreviation = QString::fromLatin1(timezoneData.abbreviation(QDateTime::currentDateTime().toUTC()));

    if (m_timezoneAbbreviation.isEmpty()) {
        m_timezoneAbbreviation = i18n("UTC");
    }

    m_timezoneArea = i18n(timezoneData.name().toUtf8().data()).replace(QChar('_'), QChar(' ')).split(QChar('/'));

    int seconds = timezoneData.currentOffset(Qt::UTC);
    int minutes = abs(seconds / 60);
    int hours = abs(minutes / 60);

    minutes = (minutes - (hours * 60));

    m_timezoneOffset = QString::number(hours);

    if (minutes) {
        m_timezoneOffset.append(QChar(':'));
        m_timezoneOffset.append(formatNumber(minutes, 2));
    }

    m_timezoneOffset = (QChar((seconds >= 0) ? QChar('+') : QChar('-')) + m_timezoneOffset);

    dataUpdated(QString(), m_applet->dataEngine("time")->query(m_applet->currentTimezone()), true);
}

void Clock::updateEvents()
{
    Plasma::DataEngine::Data eventsData = m_applet->dataEngine("calendar")->query(QString("events:%1:%2").arg(QDate::currentDate().toString(Qt::ISODate)).arg(QDate::currentDate().addDays(1).toString(Qt::ISODate)));

    m_eventsShort = m_eventsLong = QString();

    if (eventsData.isEmpty()) {
        return;
    }

    QHash<QString, QVariant>::iterator i;
    QStringList eventsShort;
    QStringList eventsLong;
    QPair<QDateTime, QDateTime> limits = qMakePair(QDateTime::currentDateTime().addSecs(-43200), QDateTime::currentDateTime().addSecs(43200));

    for (i = eventsData.begin(); i != eventsData.end(); ++i) {
        QVariantHash event = i.value().toHash();

        if (event["Type"] == "Event" || event["Type"] == "Todo") {
            KDateTime startTime = event["StartDate"].value<KDateTime>();
            KDateTime endTime = event["EndDate"].value<KDateTime>();

            if ((endTime.isValid() && endTime.dateTime() < limits.first && endTime != startTime) || startTime.dateTime() > limits.second) {
                continue;
            }

            QString type = ((event["Type"] == "Event") ? i18n("Event") : i18n("To do"));
            QString time;

            if (startTime.time().hour() == 0 && startTime.time().minute() == 0 && endTime.time().hour() == 0 && endTime.time().minute() == 0) {
                time = i18n("All day");
            } else if (startTime.isValid()) {
                time = KGlobal::locale()->formatTime(startTime.time(), false);

                if (endTime.isValid()) {
                    time.append(QString(" - %1").arg(KGlobal::locale()->formatTime(endTime.time(), false)));
                }
            }

            eventsShort.append(QString("<td align=\"right\"><nobr><i>%1</i>:</nobr></td><td align=\"left\">%2</td>").arg(type).arg(event["Summary"].toString()));
            eventsLong.append(QString("<td align=\"right\"><nobr><i>%1</i>:</nobr></td><td align=\"left\">%2 <nobr>(%3)</nobr></td>").arg(type).arg(event["Summary"].toString()).arg(time));
        }
    }

    m_eventsShort = QString("<table>\n<tr>%1</tr>\n</table>").arg(eventsShort.join("</tr>\n<tr>"));
    m_eventsLong = QString("<table>\n<tr>%1</tr>\n</table>").arg(eventsLong.join("</tr>\n<tr>"));
}

void Clock::updateHolidays()
{
    const QString region = m_applet->config().readEntry("holidaysRegions", m_applet->dataEngine("calendar")->query("holidaysDefaultRegion")["holidaysDefaultRegion"]).toString().split(QChar(',')).first();
    const QString key = QString("holidays:%1:%2").arg(region).arg(getCurrentDateTime().date().toString(Qt::ISODate));
    Plasma::DataEngine::Data holidaysData = m_applet->dataEngine("calendar")->query(key);

    m_holidays.clear();

    if (!holidaysData.isEmpty() && holidaysData.contains(key)) {
        QVariantList holidaysList = holidaysData[key].toList();
        QStringList holidays;

        for (int i = 0; i < holidaysList.length(); ++i) {
            m_holidays.append(holidaysList[i].toHash()["Name"].toString());
        }
    }
}

void Clock::applyRule(const PlaceholderRule &rule)
{
    if (m_document) {
        setValue(m_document->findAllElements(rule.rule), rule.attribute, m_engine.evaluate(rule.expression).toString());
    }
}

void Clock::setDocument(QWebFrame *document)
{
    m_document = document;

    reset();
}

void Clock::setRule(const QString &rule, const QString &attribute, const QString &expression, IntervalAlignment alignment)
{
}

void Clock::setRule(const QString &rule, const QString &expression, IntervalAlignment alignment)
{
    setRule(rule, QString(), expression, alignment);
}

void Clock::setValue(const QString &rule, const QString &attribute, const QString &value)
{
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

void Clock::setValue(const QWebElementCollection &elements, const QString &value)
{
    setValue(elements, QString(), value);
}

QDateTime Clock::getCurrentDateTime(bool refresh) const
{
    if (!refresh || m_features & SecondsClockFeature) {
        return m_dateTime;
    }

    Plasma::DataEngine::Data data = m_applet->dataEngine("time")->query(m_applet->currentTimezone());

    return QDateTime(data["Date"].toDate(), data["Time"].toTime());
}

QString Clock::extractNumber(const QString &format, int &i)
{
    QString number;

    while ((format.at(i).isDigit() || format.at(i) == QChar('-')) && i < format.length()) {
        number.append(format.at(i));

        ++i;
    }

    return number;
}

QString Clock::formatNumber(int number, int length)
{
    return QString("%1").arg(number, length, 10, QChar('0'));
}

QString Clock::evaluateFormat(const QString &format, QDateTime dateTime, bool special)
{
    if (format.isEmpty()) {
        return QString();
    }

    QString string;

    for (int i = 0; i < format.length(); ++i) {
        if (format.at(i) != QChar('%')) {
            string.append(format.at(i));

            continue;
        }

        QString substitution;
        QPair<int, int> range = qMakePair(-1, -1);
        const int start = i;
        int alternativeForm = 0;
        bool shortForm = false;
        bool textualForm = false;
        bool exclude = false;

        ++i;

        if (format.at(i) == QChar('~')) {
            ++i;

            exclude = true;
        }

        if (format.at(i).isDigit() || ((format.at(i) == QChar('-') || format.at(i) == QChar(':')) && format.at(i + 1).isDigit())) {
            if (format.at(i) == QChar(':')) {
                range.first = 0;
            } else {
                range.first = extractNumber(format, i).toInt();
            }

            if (format.at(i) == QChar(':')) {
                range.second = extractNumber(format, ++i).toInt();
            }
        }

        if (format.at(i) == QChar('!')) {
            ++i;

            shortForm = true;
        }

        if (format.at(i) == QChar('$')) {
            ++i;

            textualForm = true;
        }

        if (format.at(i) == QChar('+')) {
            ++i;

            alternativeForm = 1;
        } else if (format.at(i) == QChar('-')) {
            ++i;

            alternativeForm = -1;
        }

        if (!format.at(i).isLetter()) {
            if (format.at(i - 1) != QChar('%')) {
                string.append(format.at(i - 1));
            }

            string.append(format.at(i));

            continue;
        } else {
            if (dateTime.isValid()) {
                substitution = evaluatePlaceholder(format.at(i).unicode(), dateTime, alternativeForm, shortForm, textualForm);
            } else {
                substitution = evaluatePlaceholder(format.at(i).unicode(), alternativeForm, shortForm, textualForm);
            }
        }

        if (range.first != -1 || range.second != -1) {
            if (range.first < 0) {
                range.first = (substitution.length() + range.first);
            }

            if (range.second < -1) {
                range.second = (substitution.length() + range.second);
            }

            substitution = substitution.mid(range.first, range.second);
        }

        if (special) {
            if (exclude) {
                substitution = format.mid(start, (1 + i - start));
            } else {
                QString title;

                switch (format.at(i).unicode()) {
                case 's':
                    title = i18n("Second");

                    break;
                case 'm':
                    title = i18n("Minute");

                    break;
                case 'h':
                    title = i18n("Hour");

                    break;
                case 'p':
                    title = i18n("The pm or am string");

                    break;
                case 'd':
                    title = i18n("Day of the month");

                    break;
                case 'w':
                    title = i18n("Weekday");

                    break;
                case 'D':
                    title = i18n("Day of the year");

                    break;
                case 'W':
                    title = i18n("Week");

                    break;
                case 'M':
                    title = i18n("Month");

                    break;
                case 'Y':
                    title = i18n("Year");

                    break;
                case 'U':
                    title = i18n("UNIX timestamp");

                    break;
                case 't':
                    title = i18n("Time");

                    break;
                case 'T':
                    title = i18n("Date");

                    break;
                case 'A':
                    title = i18n("Date and time");

                    break;
                case 'z':
                    title = i18n("Timezone");

                    break;
                case 'Z':
                    title = i18n("Timezones list");

                    break;
                case 'H':
                    title = i18n("Holidays list");

                    break;
                case 'E':
                    title = i18n("Events list");

                    break;
                case 'R':
                    title = i18n("Sunrise time");

                    break;
                case 'S':
                    title = i18n("Sunset time");

                    break;
                default:
                    break;
                }

                substitution = QString("<placeholder title=\"%1\" alt=\"%2\">%3</placeholder>").arg(title).arg(format.mid(start, (1 + i - start))).arg(substitution);
            }
        }

        string.append(substitution);
    }

    return string;
}

QString Clock::evaluatePlaceholder(ushort placeholder, QDateTime dateTime, int alternativeForm, bool shortForm, bool textualForm)
{
    QStringList timezones;

    switch (placeholder) {
    case 's': // Second
        return formatNumber(dateTime.time().second(), (shortForm ? 0 : 2));
    case 'm': // Minute
        return formatNumber(dateTime.time().minute(), (shortForm ? 0 : 2));
    case 'h': // Hour
        alternativeForm = ((alternativeForm == 0) ? KGlobal::locale()->use12Clock() : (alternativeForm == 1));

        return formatNumber((alternativeForm ? (((dateTime.time().hour() + 11) % 12) + 1) : dateTime.time().hour()), (shortForm ? 0 : 2));
    case 'p': // The pm or am string
        return ((dateTime.time().hour() >= 12) ? i18n("pm") : i18n("am"));
    case 'd': // Day of the month
        return formatNumber(dateTime.date().day(), (shortForm ? 0 : 2));
    case 'w': // Weekday
        if (textualForm) {
            return m_applet->calendar()->weekDayName(m_applet->calendar()->dayOfWeek(dateTime.date()), (shortForm ? KCalendarSystem::ShortDayName : KCalendarSystem::LongDayName));
        }

        return formatNumber(m_applet->calendar()->dayOfWeek(dateTime.date()), (shortForm ? 0 : QString::number(m_applet->calendar()->daysInWeek(dateTime.date())).length()));
    case 'D': // Day of the year
        return formatNumber(m_applet->calendar()->dayOfYear(dateTime.date()), (shortForm ? 0 : QString::number(m_applet->calendar()->daysInYear(dateTime.date())).length()));
    case 'W': // Week
        return m_applet->calendar()->formatDate(dateTime.date(), KLocale::Week, (shortForm ? KLocale::ShortNumber : KLocale::LongNumber));
    case 'M': // Month
        if (textualForm) {
            alternativeForm = ((alternativeForm == 0) ? KGlobal::locale()->dateMonthNamePossessive() : (alternativeForm == 1));

            return m_applet->calendar()->monthName(dateTime.date(), (shortForm ? (alternativeForm ? KCalendarSystem::ShortNamePossessive : KCalendarSystem::ShortName) : (alternativeForm ? KCalendarSystem::LongNamePossessive : KCalendarSystem::LongName)));
        }

        return m_applet->calendar()->formatDate(dateTime.date(), KLocale::Month, (shortForm ? KLocale::ShortNumber : KLocale::LongNumber));
    case 'Y': // Year
        return m_applet->calendar()->formatDate(dateTime.date(), KLocale::Year, (shortForm ? KLocale::ShortNumber : KLocale::LongNumber));
    case 'U': // UNIX timestamp
        return QString::number(dateTime.toTime_t());
    case 't': // Time
        return KGlobal::locale()->formatTime(dateTime.time(), !shortForm);
    case 'T': // Date
        return KGlobal::locale()->formatDate(dateTime.date(), (shortForm ? KLocale::ShortDate : KLocale::LongDate));
    case 'A': // Date and time
        return KGlobal::locale()->formatDateTime(dateTime, (shortForm ? KLocale::ShortDate : KLocale::LongDate));
    case 'z': // Timezone
        if (textualForm) {
            if (alternativeForm) {
                return m_timezoneAbbreviation;
            }

            return (shortForm ? (m_timezoneArea.isEmpty() ? QString() : m_timezoneArea.last()) : m_timezoneArea.join(QString(QChar('/'))));
        }

        return m_timezoneOffset;
    case 'Z':
        timezones = m_applet->config().readEntry("timeZones", QStringList());
        timezones.prepend("");

        if (timezones.length() == 1) {
            return QString();
        }

        for (int i = 0; i < timezones.length(); ++i) {
            QString timezone = i18n((timezones.at(i).isEmpty() ? KSystemTimeZones::local() : KSystemTimeZones::zone(timezones.at(i))).name().toUtf8().data()).replace(QChar('_'), QChar(' '));

            if (shortForm && timezone.contains(QChar('/'))) {
                timezone = timezone.split(QChar('/')).last();
            }

            Plasma::DataEngine::Data data = m_applet->dataEngine("time")->query(timezones.at(i));

            timezones[i] = QString("<td align=\"right\"><nobr><i>%1</i>:</nobr></td><td align=\"left\"><nobr>%2 %3</nobr></td>").arg(timezone).arg(KGlobal::locale()->formatTime(data["Time"].toTime(), false)).arg(KGlobal::locale()->formatDate(data["Date"].toDate(), KLocale::LongDate));
        }

        return QString("<table>\n<tr>%1</tr>\n</table>").arg(timezones.join("</tr>\n<tr>"));
    case 'E': // Events list
        if (!(m_features & EventsFeature)) {
            updateEvents();
        }

        return (shortForm ? m_eventsShort : m_eventsLong);
    case 'H': // Holidays list
        if (!(m_features & HolidaysFeature)) {
            updateHolidays();
        }

        return (shortForm ? (m_holidays.isEmpty() ? QString() : m_holidays.last()) : m_holidays.join("<br>\n"));
    case 'R': // Sunrise time
        return KGlobal::locale()->formatTime(m_sunrise, false);
    case 'S': // Sunset time
        return KGlobal::locale()->formatTime(m_sunset, false);
    default:
        return QString(placeholder);
    }

    return QString();
}

QString Clock::evaluatePlaceholder(ushort placeholder, int alternativeForm, bool shortForm, bool textualForm)
{
    QString longest;
    QString temporary;
    int amount;

    switch (placeholder) {
    case 's':
    case 'm':
    case 'h':
    case 'd':
        return "00";
    case 'p':
        return ((i18n("pm").length() > i18n("am").length()) ? i18n("pm") : i18n("am"));
    case 'w':
        if (textualForm) {
            amount = m_applet->calendar()->daysInWeek(m_dateTime.date());

            for (int i = 0; i <= amount; ++i) {
                temporary = m_applet->calendar()->weekDayName(i, (shortForm ? KCalendarSystem::ShortDayName : KCalendarSystem::LongDayName));

                if (temporary.length() > longest.length()) {
                    longest = temporary;
                }
            }

            return longest;
        }

        return QString(QChar('0')).repeated(QString::number(m_applet->calendar()->daysInWeek(m_dateTime.date())).length());
    case 'D':
        return QString(QChar('0')).repeated(QString::number(m_applet->calendar()->daysInYear(m_dateTime.date())).length());
    case 'W':
        return QString(QChar('0')).repeated(QString::number(m_applet->calendar()->weeksInYear(m_dateTime.date())).length());
    case 'M':
        if (textualForm) {
            alternativeForm = ((alternativeForm == 0) ? KGlobal::locale()->dateMonthNamePossessive() : (alternativeForm == 1));

            amount = m_applet->calendar()->monthsInYear(m_dateTime.date());

            for (int i = 0; i < amount; ++i) {
                temporary = m_applet->calendar()->monthName(i, m_applet->calendar()->year(m_dateTime.date()), (shortForm ? (alternativeForm ? KCalendarSystem::ShortNamePossessive : KCalendarSystem::ShortName) : (alternativeForm ? KCalendarSystem::LongNamePossessive : KCalendarSystem::LongName)));

                if (temporary.length() > longest.length()) {
                    longest = temporary;
                }
            }

            return longest;
        }

        return QString(QChar('0')).repeated(QString::number(m_applet->calendar()->monthsInYear(m_dateTime.date())).length());
    case 'Y':
        return (shortForm ? "00" : "0000");
    case 'U':
        return QString(QChar('0')).repeated(QString::number(m_dateTime.toTime_t()).length());
    case 't':
    case 'T':
    case 'A':
    case 'z':
    case 'E':
    case 'H':
        return evaluatePlaceholder(placeholder, QDateTime::currentDateTime(), alternativeForm, shortForm, textualForm);
    case 'R':
    case 'S':
        return KGlobal::locale()->formatTime(QTime(), false);
    default:
        return QString(placeholder);
    }

    return QString();
}

QString Clock::getTimeString(ClockTimeValue type, ValueOptions options) const
{
    return QString();
}

QVariantList Clock::getEventsList(ClockEventsType type, ValueOptions options) const
{
    return QVariantList();
}

}
