#pragma once

#include <Core/Graphics/Geometry.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/Status.h>
#include <GameComponentsPlugin/Text/TextMeshComponent.h>

/// \brief Everything needed to turn a string into an extruded 3D mesh.
struct plTextMeshOptions
{
  plString m_sFontFile;                    ///< Path to a TrueType / OpenType font, resolved through the data directories.
  plString m_sText;                        ///< The text to build. Both real line breaks and the literal sequence "\n" start a new line.
  float m_fSize = 1.0f;                    ///< World size of the font's em square, ie. roughly the height of a capital letter plus its overshoot.
  float m_fExtrude = 0.1f;                 ///< Thickness of the letters. Zero produces a flat, single sided mesh.
  float m_fLetterSpacing = 0.0f;           ///< Additional advance between glyphs, in multiples of m_fSize.
  float m_fLineSpacing = 1.0f;             ///< Multiplier for the font's own line height.
  plUInt16 m_uiCurveDetail = 0;            ///< Segments used to flatten each curve segment. 0 uses a sensible default.
  plEnum<plTextMeshAlignment> m_Alignment; ///< Horizontal alignment of each line.
  bool m_bCenterVertically = true;         ///< Center the whole text block vertically on the origin instead of putting the first baseline there.
};

/// \brief Builds extruded 3D text into \a ref_geom.
///
/// The glyphs come out in the engine's own coordinate system: the text stands upright with +Z up,
/// advances along +Y and its front faces point along +X. Since +X is an object's forward direction
/// and +Y its left, the text reads correctly for someone looking at the front of the object.
/// \a geoOptions is applied on top of that.
plStatus plGenerateTextGeometry(const plTextMeshOptions& options, const plGeometry::GeoOptions& geoOptions, plGeometry& ref_geom);
