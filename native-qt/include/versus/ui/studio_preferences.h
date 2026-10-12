#pragma once
#include <QApplication>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAction>
#include <QEvent>
#include <QFile>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QRegularExpression>
#include <QSettings>
#include <QTranslator>

namespace versus::ui {
inline QString studioText(const char *source) { return QCoreApplication::translate("Studio", source); }
class StudioTranslator final : public QTranslator {
    QJsonObject strings_;
public:
    explicit StudioTranslator(QObject *parent) : QTranslator(parent) {
        const auto language = QSettings("VDO.Ninja", "Video Mixer").value("ui/language", "en").toString();
        qApp->setProperty("studioLanguage", language);
        if (language != "es" && language != "fr") return;
        QFile file(":/mixer/studio-"+language+".json");
        if (file.open(QIODevice::ReadOnly)) strings_ = QJsonDocument::fromJson(file.readAll()).object();
    }
    bool isEmpty() const override { return strings_.isEmpty(); }
    QString translate(const char *, const char *source, const char *, int) const override {
        return strings_.value(QString::fromUtf8(source)).toString();
    }
};

// Metadata is independent of painting. Keep existing focus styles and widget
// geometry exactly as they are. New dialogs/audio rows receive metadata too.
inline void accessibleWidget(QWidget *widget) {
    if (widget->accessibleDescription().isEmpty() && !widget->toolTip().isEmpty())
        widget->setAccessibleDescription(widget->toolTip());
    if (!widget->accessibleName().isEmpty()) return;
    static const QHash<QString, QString> names{
        {"addScene", "Add scene"}, {"removeScene", "Remove scene"}, {"sceneList", "Scenes"},
        {"mixerSources", "Sources in selected scene"}, {"layerX", "Layer horizontal position"},
        {"layerY", "Layer vertical position"}, {"layerW", "Layer width"}, {"layerH", "Layer height"},
        {"cropLeft", "Crop left"}, {"cropRight", "Crop right"}, {"cropTop", "Crop top"}, {"cropBottom", "Crop bottom"},
        {"canvasFormat", "Canvas resolution and frame rate"}, {"compositorBackend", "Compositor"},
        {"layerFit", "Layer scaling"}, {"layerOpacity", "Layer opacity"}, {"masterGain", "Master volume"},
        {"monitorDevice", "Headphone output device"}, {"monitorGain", "Headphone volume"}
    };
    if (names.contains(widget->objectName())) {
        const auto name = studioText(names[widget->objectName()].toUtf8().constData());
        widget->setAccessibleName(name);
        if (widget->toolTip().isEmpty()) widget->setToolTip(name);
    } else if (auto *button = qobject_cast<QAbstractButton*>(widget)) {
        if (button->text().size() > 1) widget->setAccessibleName(button->text().remove('&'));
    } else if (auto *parent = widget->parentWidget()) {
        if (auto *form = qobject_cast<QFormLayout*>(parent->layout()))
            if (auto *label = qobject_cast<QLabel*>(form->labelForField(widget))) {
                label->setBuddy(widget); widget->setAccessibleName(label->text().remove('&'));
            }
    }
}
class StudioAppearance final : public QObject {
    QString theme_ = "original", applicationStyle_;
    QString recolor(QString css) const {
        if (theme_ == "original") return css;
        QString bg, panel, input, button, accent, hover, dim;
        if (theme_ == "rose") { bg="#211821"; panel="#2b202b"; input="#342634"; button="#433045"; accent="#f5a9d0"; hover="#ffd1e6"; dim="#cfb5c8"; }
        else if (theme_ == "neon") { bg="#141321"; panel="#1c1a30"; input="#24223b"; button="#302a4c"; accent="#b6a1ff"; hover="#d0c2ff"; dim="#b7b0d2"; }
        else if (theme_ == "sand") { bg="#211e1a"; panel="#2a2620"; input="#332d25"; button="#40372b"; accent="#e7bd7c"; hover="#f8d7a5"; dim="#c8bca9"; }
        else { bg="#191c20"; panel="#22262b"; input="#2b3037"; button="#333b44"; accent="#9dc6e7"; hover="#c6e0f4"; dim="#b5c0cd"; }
        const QHash<QString,QString> colors{
            {"#0b1016",bg}, {"#3dd8ff",hover}, {"#89a2ba",dim},
            {"#0a0e14",bg}, {"#0b121a",bg}, {"#0b141e",panel}, {"#0d151f",panel},
            {"#101b27",panel}, {"#0e1822",panel}, {"#101820",panel}, {"#0d1721",panel},
            {"#0f1822",input}, {"#142332",input}, {"#162634",button}, {"#192b3d",button},
            {"#253c52",button}, {"#16435a",button}, {"#00c2ff",accent}, {"#00a8df",hover},
            {"#56dcff",accent}, {"#7ca8ff",accent}, {"#9ab5e8",dim}, {"#9bb0c4",dim}
        };
        static const QRegularExpression hex("#[0-9a-fA-F]{6}");
        auto matches = hex.globalMatch(css); QList<QRegularExpressionMatch> all;
        while (matches.hasNext()) all.append(matches.next());
        for (auto it=all.crbegin(); it!=all.crend(); ++it)
            if (colors.contains(it->captured().toLower())) css.replace(it->capturedStart(),it->capturedLength(),colors[it->captured().toLower()]);
        return css;
    }
    void style(QWidget *widget) {
        if (!widget->property("studioBaseStyle").isValid()) widget->setProperty("studioBaseStyle", widget->styleSheet());
        const auto css = recolor(widget->property("studioBaseStyle").toString());
        if (widget->styleSheet() != css) widget->setStyleSheet(css);
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Polish) if (auto *widget = qobject_cast<QWidget*>(object)) {
            accessibleWidget(widget);
            if (theme_ != "original") style(widget);
        }
        return false;
    }
public:
    explicit StudioAppearance(QObject *parent) : QObject(parent), applicationStyle_(qApp->styleSheet()) { qApp->installEventFilter(this); }
    void apply(const QString &theme) {
        theme_ = theme; qApp->setProperty("studioTheme", theme);
        qApp->setStyleSheet(recolor(applicationStyle_));
        for (auto *widget : QApplication::allWidgets()) { accessibleWidget(widget); style(widget); }
    }
};
}
