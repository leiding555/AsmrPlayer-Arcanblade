#pragma once

#include <QPainterPath>

namespace asmr {

inline QPainterPath favoriteHeartPath()
{
    QPainterPath heart;
    heart.moveTo(12, 20.5);
    heart.cubicTo(10, 19, 2.5, 14, 2.5, 8.7);
    heart.cubicTo(2.5, 3.1, 8.9, 1.8, 12, 6.7);
    heart.cubicTo(15.1, 1.8, 21.5, 3.1, 21.5, 8.7);
    heart.cubicTo(21.5, 14, 14, 19, 12, 20.5);
    heart.closeSubpath();
    return heart;
}

} // namespace asmr
