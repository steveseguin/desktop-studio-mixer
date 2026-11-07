#pragma once
#include <QLayout>
#include <QList>
#include <algorithm>

namespace versus::mixer {
// Keep the operator's actions in order, wrapping only when their actual labels
// no longer fit. Height follows width without imposing a desktop-sized minimum.
class StudioActionLayout final : public QLayout {
    QList<QLayoutItem *> items_;
    int arrange(const QRect &rect, bool apply) const {
        const auto margins=contentsMargins();
        const auto area=rect.marginsRemoved(margins);
        int x=area.x(), y=area.y(), rowHeight=0;
        for(auto *item:items_) {
            if(item->isEmpty())continue;
            const auto size=item->sizeHint().expandedTo(item->minimumSize());
            if(rowHeight && x+size.width()>area.right()+1) {
                x=area.x();y+=rowHeight+spacing();rowHeight=0;
            }
            if(apply)item->setGeometry(QRect(QPoint(x,y),size));
            x+=size.width()+spacing();rowHeight=std::max(rowHeight,size.height());
        }
        return y-area.y()+rowHeight+margins.top()+margins.bottom();
    }
public:
    StudioActionLayout() {setContentsMargins(0,0,0,0);setSpacing(6);}
    ~StudioActionLayout() override {while(auto *item=takeAt(0))delete item;}
    void addItem(QLayoutItem *item) override {items_.append(item);invalidate();}
    int count() const override {return items_.size();}
    QLayoutItem *itemAt(int index) const override {return items_.value(index);}
    QLayoutItem *takeAt(int index) override {return index>=0&&index<items_.size()?items_.takeAt(index):nullptr;}
    Qt::Orientations expandingDirections() const override {return Qt::Horizontal;}
    bool hasHeightForWidth() const override {return true;}
    int heightForWidth(int width) const override {return arrange(QRect(0,0,width,0),false);}
    QSize minimumSize() const override {
        QSize size;for(auto *item:items_)if(!item->isEmpty())size=size.expandedTo(item->minimumSize());
        const auto m=contentsMargins();return size+QSize(m.left()+m.right(),m.top()+m.bottom());
    }
    QSize sizeHint() const override {return minimumSize();}
    void setGeometry(const QRect &rect) override {QLayout::setGeometry(rect);arrange(rect,true);}
};
} // namespace versus::mixer
