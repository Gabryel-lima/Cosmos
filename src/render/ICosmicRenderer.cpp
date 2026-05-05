// src/render/ICosmicRenderer.cpp — Estado global de QualityTier.
#include "ICosmicRenderer.hpp"

static QualityTier g_current_quality = DefaultQualityTierFromBuild();

QualityTier GetCurrentQuality()          { return g_current_quality; }
void        SetCurrentQuality(QualityTier tier) { g_current_quality = tier; }
