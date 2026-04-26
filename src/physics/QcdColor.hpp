#pragma once

#include <cstdint>

enum class QcdColor : uint8_t {
    NONE = 0,
    RED,
    GREEN,
    BLUE,
    ANTI_RED,
    ANTI_GREEN,
    ANTI_BLUE,
};

namespace qcd {

inline bool isPrimary(QcdColor color) {
    return color == QcdColor::RED || color == QcdColor::GREEN || color == QcdColor::BLUE;
}

inline bool isAnticolor(QcdColor color) {
    return color == QcdColor::ANTI_RED || color == QcdColor::ANTI_GREEN || color == QcdColor::ANTI_BLUE;
}

inline QcdColor antiColor(QcdColor color) {
    switch (color) {
        case QcdColor::RED:        return QcdColor::ANTI_RED;
        case QcdColor::GREEN:      return QcdColor::ANTI_GREEN;
        case QcdColor::BLUE:       return QcdColor::ANTI_BLUE;
        case QcdColor::ANTI_RED:   return QcdColor::RED;
        case QcdColor::ANTI_GREEN: return QcdColor::GREEN;
        case QcdColor::ANTI_BLUE:  return QcdColor::BLUE;
        default:                   return QcdColor::NONE;
    }
}

inline QcdColor baseColor(QcdColor color) {
    switch (color) {
        case QcdColor::RED:
        case QcdColor::ANTI_RED:
            return QcdColor::RED;
        case QcdColor::GREEN:
        case QcdColor::ANTI_GREEN:
            return QcdColor::GREEN;
        case QcdColor::BLUE:
        case QcdColor::ANTI_BLUE:
            return QcdColor::BLUE;
        default:
            return QcdColor::NONE;
    }
}

inline QcdColor receiverColorFromAnticolor(QcdColor anticolor) {
    return antiColor(anticolor);
}

inline float casimirFactor(QcdColor color_i, QcdColor anticolor_i,
                           QcdColor color_j, QcdColor anticolor_j)
{
    if (color_i != QcdColor::NONE && anticolor_j == antiColor(color_i)) return -4.0f / 3.0f;
    if (color_j != QcdColor::NONE && anticolor_i == antiColor(color_j)) return -4.0f / 3.0f;

    if (color_i != QcdColor::NONE && color_j != QcdColor::NONE) {
        return (baseColor(color_i) == baseColor(color_j)) ? (1.0f / 3.0f) : (-2.0f / 3.0f);
    }

    if (anticolor_i != QcdColor::NONE && anticolor_j != QcdColor::NONE) {
        return (baseColor(anticolor_i) == baseColor(anticolor_j)) ? (1.0f / 3.0f) : (-2.0f / 3.0f);
    }

    if (color_i != QcdColor::NONE && anticolor_j != QcdColor::NONE) {
        return (anticolor_j == antiColor(color_i)) ? (-4.0f / 3.0f) : (-2.0f / 3.0f);
    }

    if (color_j != QcdColor::NONE && anticolor_i != QcdColor::NONE) {
        return (anticolor_i == antiColor(color_j)) ? (-4.0f / 3.0f) : (-2.0f / 3.0f);
    }

    return 0.0f;
}

inline void rgb(QcdColor color, float& r, float& g, float& b) {
    switch (baseColor(color)) {
        case QcdColor::RED:
            r = 1.0f; g = 0.18f; b = 0.18f; break;
        case QcdColor::GREEN:
            r = 0.18f; g = 1.0f; b = 0.24f; break;
        case QcdColor::BLUE:
            r = 0.22f; g = 0.45f; b = 1.0f; break;
        default:
            r = 1.0f; g = 1.0f; b = 1.0f; break;
    }
}

} // namespace qcd
