#pragma once
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace versus::mixer {
enum class StudioIcon { Guest, Camera, Chat, Mic, Settings, Live, Record, Pause, Play, Tools, Video, Folder, Save, Undo, Redo, Popout, Plus, Layers, Title, Image, Audio };

// Small, consistent vector strokes, rasterized at native device scales. No font glyphs
// or external resources: portable and installed builds use the same pictograms.
inline QIcon studioIcon(StudioIcon symbol, QColor ink = QColor("#bed0e2")) {
    QIcon icon;
    for(int scale : {1,2,3}) {
        QPixmap pixmap(20*scale,20*scale);pixmap.setDevicePixelRatio(scale);pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(ink,1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);
        auto line=[&](qreal x1,qreal y1,qreal x2,qreal y2){p.drawLine(QPointF(x1,y1),QPointF(x2,y2));};
        auto path=[&](std::initializer_list<QPointF> points){QPainterPath q;bool first=true;for(auto point:points){if(first)q.moveTo(point);else q.lineTo(point);first=false;}p.drawPath(q);};
        switch(symbol) {
        case StudioIcon::Guest:p.drawEllipse(QRectF(4,2,6,6));p.drawArc(QRectF(1,9,12,13),0,180*16);line(15,6,15,12);line(12,9,18,9);break;
        case StudioIcon::Camera:p.drawRoundedRect(QRectF(2,5,11,11),2,2);path({{13,8},{18,5},{18,16},{13,13}});break;
        case StudioIcon::Chat:path({{3,3},{17,3},{17,13},{9,13},{5,17},{5,13},{3,13},{3,3}});line(6,7,14,7);line(6,10,11,10);break;
        case StudioIcon::Mic:p.drawRoundedRect(QRectF(7,2,6,10),3,3);p.drawArc(QRectF(4,5,12,11),180*16,180*16);line(10,16,10,18);line(7,18,13,18);break;
        case StudioIcon::Settings:case StudioIcon::Tools:line(3,5,17,5);line(3,10,17,10);line(3,15,17,15);p.setBrush(QColor("#142334"));p.drawEllipse(QRectF(6,3,4,4));p.drawEllipse(QRectF(11,8,4,4));p.drawEllipse(QRectF(4,13,4,4));break;
        case StudioIcon::Live:p.drawEllipse(QRectF(8,8,4,4));p.drawArc(QRectF(4,4,12,12),-65*16,130*16);p.drawArc(QRectF(4,4,12,12),115*16,130*16);p.drawArc(QRectF(1,1,18,18),-60*16,120*16);p.drawArc(QRectF(1,1,18,18),120*16,120*16);break;
        case StudioIcon::Record:p.setBrush(ink);p.drawEllipse(QRectF(5,5,10,10));break;
        case StudioIcon::Pause:line(7,4,7,16);line(13,4,13,16);break;
        case StudioIcon::Play:path({{6,3},{17,10},{6,17},{6,3}});break;
        case StudioIcon::Video:p.drawRoundedRect(QRectF(2,3,16,12),2,2);line(7,18,13,18);line(10,15,10,18);break;
        case StudioIcon::Folder:path({{2,5},{8,5},{10,7},{18,7},{17,16},{3,16},{2,5}});break;
        case StudioIcon::Save:path({{3,3},{14,3},{17,6},{17,17},{3,17},{3,3}});p.drawRect(QRectF(6,3,7,5));p.drawRect(QRectF(6,12,8,5));break;
        case StudioIcon::Undo:case StudioIcon::Redo:{if(symbol==StudioIcon::Redo){p.translate(20,0);p.scale(-1,1);}path({{7,3},{3,7},{7,11}});QPainterPath q;q.moveTo(3,7);q.lineTo(12,7);q.cubicTo(19,7,19,17,12,17);p.drawPath(q);break;}
        case StudioIcon::Popout:path({{11,3},{17,3},{17,9}});line(17,3,9,11);path({{7,4},{3,4},{3,17},{16,17},{16,13}});break;
        case StudioIcon::Plus:line(10,3,10,17);line(3,10,17,10);break;
        case StudioIcon::Layers:path({{2,7},{10,3},{18,7},{10,11},{2,7}});path({{2,11},{10,15},{18,11}});path({{2,15},{10,19},{18,15}});break;
        case StudioIcon::Title:line(3,4,17,4);line(10,4,10,17);line(7,17,13,17);line(3,4,3,7);line(17,4,17,7);break;
        case StudioIcon::Image:p.drawRoundedRect(QRectF(2,3,16,14),2,2);p.drawEllipse(QRectF(5,6,3,3));path({{3,16},{10,10},{17,16}});break;
        case StudioIcon::Audio:line(3,8,3,12);line(7,4,7,16);line(11,6,11,14);line(15,2,15,18);line(19,8,19,12);break;
        }
        p.end();icon.addPixmap(pixmap);
    }
    return icon;
}
}
