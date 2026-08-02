#include <GameComponentsPlugin/GameComponentsPCH.h>

#include <Foundation/IO/FileSystem/FileReader.h>
#include <GameComponentsPlugin/Text/Implementation/TextMeshGenerator.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

namespace
{
  /// All 2D math in this file happens in 'em units', ie. font units divided by the font's units-per-em.
  /// That way a capital letter is roughly 0.7 units tall and everything scales linearly with the requested text size.

  constexpr plUInt32 s_uiMaxGlyphs = 1024;
  constexpr float s_fWeldDistance = 0.00002f;    // em units; merges points that a font stored twice
  constexpr float s_fMinContourArea = 1e-8f;     // em units squared; drops degenerate contours
  constexpr float s_fEarArea = 1e-12f;           // em units squared; below this a triangle counts as collinear
  constexpr float s_fSmoothNormalCosine = 0.64f; // ~50 degrees, above this the wall normals get averaged

  struct Contour
  {
    plDynamicArray<plVec2> m_Points;
    float m_fSignedArea = 0.0f;
    bool m_bIsHole = false;
  };

  //////////////////////////////////////////////////////////////////////////
  // FreeType outline decomposition

  struct DecomposeState
  {
    plDynamicArray<Contour>* m_pContours = nullptr;
    plDynamicArray<plVec2> m_CurrentPoints;
    plVec2 m_vCursor = plVec2::MakeZero();
    float m_fScale = 1.0f;
    plUInt32 m_uiCurveSegments = 8;

    plVec2 ToEmUnits(const FT_Vector* pVec) const { return plVec2(static_cast<float>(pVec->x), static_cast<float>(pVec->y)) * m_fScale; }

    void Add(const plVec2& vPoint)
    {
      // FreeType happily emits repeated points, and poly2tri rejects them
      if (!m_CurrentPoints.IsEmpty() && (m_CurrentPoints.PeekBack() - vPoint).GetLengthSquared() <= s_fWeldDistance * s_fWeldDistance)
        return;

      m_CurrentPoints.PushBack(vPoint);
    }

    void FinishContour()
    {
      if (m_CurrentPoints.GetCount() >= 3)
      {
        // the outline is implicitly closed, so a duplicated end point has to go
        while (m_CurrentPoints.GetCount() >= 3 && (m_CurrentPoints.PeekBack() - m_CurrentPoints[0]).GetLengthSquared() <= s_fWeldDistance * s_fWeldDistance)
        {
          m_CurrentPoints.PopBack();
        }
      }

      if (m_CurrentPoints.GetCount() >= 3)
      {
        Contour& c = m_pContours->ExpandAndGetRef();
        c.m_Points = m_CurrentPoints;
      }

      m_CurrentPoints.Clear();
    }
  };

  int DecomposeMoveTo(const FT_Vector* to, void* pUser)
  {
    auto& state = *static_cast<DecomposeState*>(pUser);
    state.FinishContour();

    state.m_vCursor = state.ToEmUnits(to);
    state.Add(state.m_vCursor);
    return 0;
  }

  int DecomposeLineTo(const FT_Vector* to, void* pUser)
  {
    auto& state = *static_cast<DecomposeState*>(pUser);

    state.m_vCursor = state.ToEmUnits(to);
    state.Add(state.m_vCursor);
    return 0;
  }

  int DecomposeConicTo(const FT_Vector* control, const FT_Vector* to, void* pUser)
  {
    auto& state = *static_cast<DecomposeState*>(pUser);

    const plVec2 p0 = state.m_vCursor;
    const plVec2 p1 = state.ToEmUnits(control);
    const plVec2 p2 = state.ToEmUnits(to);

    for (plUInt32 i = 1; i <= state.m_uiCurveSegments; ++i)
    {
      const float t = static_cast<float>(i) / static_cast<float>(state.m_uiCurveSegments);
      const float it = 1.0f - t;

      state.Add(p0 * (it * it) + p1 * (2.0f * it * t) + p2 * (t * t));
    }

    state.m_vCursor = p2;
    return 0;
  }

  int DecomposeCubicTo(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to, void* pUser)
  {
    auto& state = *static_cast<DecomposeState*>(pUser);

    const plVec2 p0 = state.m_vCursor;
    const plVec2 p1 = state.ToEmUnits(control1);
    const plVec2 p2 = state.ToEmUnits(control2);
    const plVec2 p3 = state.ToEmUnits(to);

    for (plUInt32 i = 1; i <= state.m_uiCurveSegments; ++i)
    {
      const float t = static_cast<float>(i) / static_cast<float>(state.m_uiCurveSegments);
      const float it = 1.0f - t;

      state.Add(p0 * (it * it * it) + p1 * (3.0f * it * it * t) + p2 * (3.0f * it * t * t) + p3 * (t * t * t));
    }

    state.m_vCursor = p3;
    return 0;
  }

  //////////////////////////////////////////////////////////////////////////
  // 2D helpers

  float ComputeSignedArea(const plDynamicArray<plVec2>& points)
  {
    float fArea = 0.0f;

    for (plUInt32 i = 0; i < points.GetCount(); ++i)
    {
      const plVec2& a = points[i];
      const plVec2& b = points[(i + 1) % points.GetCount()];

      fArea += a.x * b.y - b.x * a.y;
    }

    return fArea * 0.5f;
  }

  bool IsPointInside(const plVec2& vPoint, const plDynamicArray<plVec2>& polygon)
  {
    bool bInside = false;

    for (plUInt32 i = 0, j = polygon.GetCount() - 1; i < polygon.GetCount(); j = i++)
    {
      const plVec2& a = polygon[i];
      const plVec2& b = polygon[j];

      if ((a.y > vPoint.y) != (b.y > vPoint.y))
      {
        const float fT = (vPoint.y - a.y) / (b.y - a.y);

        if (vPoint.x < a.x + fT * (b.x - a.x))
          bInside = !bInside;
      }
    }

    return bInside;
  }

  /// \brief Contours can touch the polygon they are tested against, so a single sample point is not reliable. Let all points vote.
  bool IsContourInside(const Contour& contour, const Contour& other)
  {
    plUInt32 uiInside = 0;

    for (const plVec2& p : contour.m_Points)
    {
      if (IsPointInside(p, other.m_Points))
        ++uiInside;
    }

    return uiInside * 2 > contour.m_Points.GetCount();
  }

  void EnsureWinding(plDynamicArray<plVec2>& ref_points, bool bCounterClockwise)
  {
    const bool bIsCounterClockwise = ComputeSignedArea(ref_points) > 0.0f;

    if (bIsCounterClockwise != bCounterClockwise)
    {
      for (plUInt32 i = 0; i < ref_points.GetCount() / 2; ++i)
      {
        plMath::Swap(ref_points[i], ref_points[ref_points.GetCount() - 1 - i]);
      }
    }
  }

  //////////////////////////////////////////////////////////////////////////
  // The reusable geometry of a single glyph, in em units with the baseline origin at (0, 0)

  struct WallEdge
  {
    PL_DECLARE_POD_TYPE();

    plVec2 m_vPosA;
    plVec2 m_vPosB;
    plVec2 m_vNormalA;
    plVec2 m_vNormalB;
    float m_fDistanceA;
    float m_fDistanceB;
  };

  struct GlyphMesh
  {
    plDynamicArray<plVec2> m_CapPoints;
    plDynamicArray<plUInt32> m_CapIndices; // triples, counter clockwise when seen from +Z
    plDynamicArray<WallEdge> m_WallEdges;
    plVec2 m_vBoundsMin = plVec2::MakeZero();
    plVec2 m_vBoundsMax = plVec2::MakeZero();
    float m_fAdvance = 0.0f;
    bool m_bHasGeometry = false;
  };

  /// \brief Groups the contours of one glyph into filled shapes plus their holes.
  ///
  /// Nesting is resolved by depth: a contour that is inside an odd number of other contours is a hole
  /// of its innermost enclosing contour, everything else starts a new filled shape. That handles both
  /// TrueType and PostScript winding conventions without having to trust the font's orientation flags.
  void GroupContours(plDynamicArray<Contour>& ref_contours, plDynamicArray<plHybridArray<plUInt32, 4>>& out_groups)
  {
    plHybridArray<plUInt32, 16> depths;
    plHybridArray<plInt32, 16> parents;
    depths.SetCount(ref_contours.GetCount());
    parents.SetCount(ref_contours.GetCount());

    for (plUInt32 i = 0; i < ref_contours.GetCount(); ++i)
    {
      plUInt32 uiDepth = 0;
      plInt32 iParent = -1;
      float fSmallestParentArea = plMath::MaxValue<float>();

      for (plUInt32 j = 0; j < ref_contours.GetCount(); ++j)
      {
        if (i == j)
          continue;

        if (!IsContourInside(ref_contours[i], ref_contours[j]))
          continue;

        ++uiDepth;

        // the innermost enclosing contour is the one with the smallest area
        const float fArea = plMath::Abs(ref_contours[j].m_fSignedArea);
        if (fArea < fSmallestParentArea)
        {
          fSmallestParentArea = fArea;
          iParent = static_cast<plInt32>(j);
        }
      }

      depths[i] = uiDepth;
      parents[i] = iParent;
    }

    // every even depth starts a filled shape
    plHybridArray<plInt32, 16> groupOfContour;
    groupOfContour.SetCount(ref_contours.GetCount(), -1);

    for (plUInt32 i = 0; i < ref_contours.GetCount(); ++i)
    {
      if ((depths[i] % 2) != 0)
        continue;

      ref_contours[i].m_bIsHole = false;
      groupOfContour[i] = static_cast<plInt32>(out_groups.GetCount());

      auto& group = out_groups.ExpandAndGetRef();
      group.PushBack(i);
    }

    for (plUInt32 i = 0; i < ref_contours.GetCount(); ++i)
    {
      if ((depths[i] % 2) == 0)
        continue;

      ref_contours[i].m_bIsHole = true;

      const plInt32 iParent = parents[i];
      if (iParent < 0 || groupOfContour[iParent] < 0)
        continue; // should not happen, but never write out of bounds

      out_groups[groupOfContour[iParent]].PushBack(i);
    }
  }

  //////////////////////////////////////////////////////////////////////////
  // Triangulation: every hole is bridged into the outline, then the resulting simple polygon is ear clipped.

  PL_ALWAYS_INLINE float Cross2(const plVec2& a, const plVec2& b)
  {
    return a.x * b.y - a.y * b.x;
  }

  PL_ALWAYS_INLINE bool IsSamePoint(const plVec2& a, const plVec2& b)
  {
    return (a - b).GetLengthSquared() <= s_fWeldDistance * s_fWeldDistance;
  }

  /// \brief Whether two segments cross. Segments that only share an end point do not count, those show up all over a bridged polygon.
  bool DoSegmentsCross(const plVec2& a0, const plVec2& a1, const plVec2& b0, const plVec2& b1)
  {
    if (IsSamePoint(a0, b0) || IsSamePoint(a0, b1) || IsSamePoint(a1, b0) || IsSamePoint(a1, b1))
      return false;

    const float d1 = Cross2(a1 - a0, b0 - a0);
    const float d2 = Cross2(a1 - a0, b1 - a0);
    const float d3 = Cross2(b1 - b0, a0 - b0);
    const float d4 = Cross2(b1 - b0, a1 - b0);

    return ((d1 > 0.0f) != (d2 > 0.0f)) && ((d3 > 0.0f) != (d4 > 0.0f));
  }

  bool IsPointInTriangle(const plVec2& p, const plVec2& a, const plVec2& b, const plVec2& c)
  {
    const float d1 = Cross2(b - a, p - a);
    const float d2 = Cross2(c - b, p - b);
    const float d3 = Cross2(a - c, p - c);

    return (d1 >= 0.0f && d2 >= 0.0f && d3 >= 0.0f) || (d1 <= 0.0f && d2 <= 0.0f && d3 <= 0.0f);
  }

  /// \brief Triangulates one filled shape (first entry of \a group) plus its holes into \a ref_mesh.
  ///
  /// The filled outline is expected to be counter clockwise and the holes clockwise, so the bridged
  /// polygon stays counter clockwise and every emitted triangle faces along +Z.
  bool TriangulateShape(const plDynamicArray<Contour>& contours, const plHybridArray<plUInt32, 4>& group, GlyphMesh& ref_mesh, plUInt32& ref_uiSkippedHoles)
  {
    const plUInt32 uiFirstCapPoint = ref_mesh.m_CapPoints.GetCount();

    // all points of this shape go into the mesh up front, everything below only deals with indices
    plHybridArray<plUInt32, 8> contourStart;
    for (plUInt32 uiContour : group)
    {
      contourStart.PushBack(ref_mesh.m_CapPoints.GetCount());

      for (const plVec2& p : contours[uiContour].m_Points)
      {
        ref_mesh.m_CapPoints.PushBack(p);
      }
    }

    auto pos = [&ref_mesh](plUInt32 uiIndex) -> plVec2
    { return ref_mesh.m_CapPoints[uiIndex]; };

    plHybridArray<plUInt32, 256> ring;
    for (plUInt32 i = 0; i < contours[group[0]].m_Points.GetCount(); ++i)
    {
      ring.PushBack(contourStart[0] + i);
    }

    plHybridArray<plVec2, 16> bridgeStart;
    plHybridArray<plVec2, 16> bridgeEnd;

    // a bridge must not leave the shape, cross any outline or run through another hole
    auto isBridgeValid = [&](const plVec2& vFrom, const plVec2& vTo) -> bool
    {
      for (plUInt32 uiContour : group)
      {
        const plDynamicArray<plVec2>& points = contours[uiContour].m_Points;

        for (plUInt32 i = 0; i < points.GetCount(); ++i)
        {
          if (DoSegmentsCross(vFrom, vTo, points[i], points[(i + 1) % points.GetCount()]))
            return false;
        }
      }

      for (plUInt32 i = 0; i < bridgeStart.GetCount(); ++i)
      {
        if (DoSegmentsCross(vFrom, vTo, bridgeStart[i], bridgeEnd[i]))
          return false;
      }

      const plVec2 vMiddle = (vFrom + vTo) * 0.5f;

      if (!IsPointInside(vMiddle, contours[group[0]].m_Points))
        return false;

      for (plUInt32 i = 1; i < group.GetCount(); ++i)
      {
        if (IsPointInside(vMiddle, contours[group[i]].m_Points))
          return false;
      }

      return true;
    };

    for (plUInt32 h = 1; h < group.GetCount(); ++h)
    {
      const plDynamicArray<plVec2>& holePoints = contours[group[h]].m_Points;

      // the rightmost point of a hole always has a clear line towards the outline
      plUInt32 uiHoleVertex = 0;
      for (plUInt32 i = 1; i < holePoints.GetCount(); ++i)
      {
        if (holePoints[i].x > holePoints[uiHoleVertex].x || (holePoints[i].x == holePoints[uiHoleVertex].x && holePoints[i].y > holePoints[uiHoleVertex].y))
        {
          uiHoleVertex = i;
        }
      }

      const plVec2 vHolePos = holePoints[uiHoleVertex];

      plInt32 iRingPosition = -1;
      float fBestDistance = plMath::MaxValue<float>();

      for (plUInt32 r = 0; r < ring.GetCount(); ++r)
      {
        const plVec2 vRingPos = pos(ring[r]);
        const float fDistance = (vRingPos - vHolePos).GetLengthSquared();

        if (fDistance >= fBestDistance)
          continue;

        if (!isBridgeValid(vHolePos, vRingPos))
          continue;

        fBestDistance = fDistance;
        iRingPosition = static_cast<plInt32>(r);
      }

      if (iRingPosition < 0)
      {
        // no way in; leaving the hole filled is better than dropping the whole letter
        ++ref_uiSkippedHoles;
        continue;
      }

      const plUInt32 uiRingVertex = ring[iRingPosition];

      // walk the hole once and come back, the two coincident bridge edges have no area
      plHybridArray<plUInt32, 256> bridged;
      for (plUInt32 i = 0; i <= static_cast<plUInt32>(iRingPosition); ++i)
      {
        bridged.PushBack(ring[i]);
      }
      for (plUInt32 i = 0; i < holePoints.GetCount(); ++i)
      {
        bridged.PushBack(contourStart[h] + (uiHoleVertex + i) % holePoints.GetCount());
      }
      bridged.PushBack(contourStart[h] + uiHoleVertex);
      bridged.PushBack(uiRingVertex);
      for (plUInt32 i = static_cast<plUInt32>(iRingPosition) + 1; i < ring.GetCount(); ++i)
      {
        bridged.PushBack(ring[i]);
      }

      ring = bridged;

      bridgeStart.PushBack(vHolePos);
      bridgeEnd.PushBack(pos(uiRingVertex));
    }

    const plUInt32 uiFirstIndex = ref_mesh.m_CapIndices.GetCount();

    plUInt32 uiIterations = ring.GetCount() * 3 + 16;

    while (ring.GetCount() > 3 && uiIterations-- > 0)
    {
      bool bRemoved = false;

      for (plUInt32 i = 0; i < ring.GetCount(); ++i)
      {
        const plUInt32 uiPrev = (i + ring.GetCount() - 1) % ring.GetCount();
        const plUInt32 uiNext = (i + 1) % ring.GetCount();

        const plVec2 a = pos(ring[uiPrev]);
        const plVec2 b = pos(ring[i]);
        const plVec2 c = pos(ring[uiNext]);

        const float fCross = Cross2(b - a, c - b);

        if (fCross <= s_fEarArea)
        {
          // a collinear vertex can be dropped without changing the shape, and it would otherwise block progress
          if (fCross >= -s_fEarArea)
          {
            ring.RemoveAtAndCopy(i);
            bRemoved = true;
            break;
          }

          continue; // reflex, not an ear
        }

        bool bIsEar = true;

        for (plUInt32 j = 0; j < ring.GetCount() && bIsEar; ++j)
        {
          if (j == uiPrev || j == i || j == uiNext)
            continue;

          const plVec2 p = pos(ring[j]);

          // bridges duplicate points, those must not veto their own ear
          if (IsSamePoint(p, a) || IsSamePoint(p, b) || IsSamePoint(p, c))
            continue;

          if (IsPointInTriangle(p, a, b, c))
            bIsEar = false;
        }

        if (!bIsEar)
          continue;

        ref_mesh.m_CapIndices.PushBack(ring[uiPrev]);
        ref_mesh.m_CapIndices.PushBack(ring[i]);
        ref_mesh.m_CapIndices.PushBack(ring[uiNext]);

        ring.RemoveAtAndCopy(i);
        bRemoved = true;
        break;
      }

      if (!bRemoved)
        break; // nothing clippable left, the rest of the ring is dropped
    }

    if (ring.GetCount() == 3)
    {
      ref_mesh.m_CapIndices.PushBack(ring[0]);
      ref_mesh.m_CapIndices.PushBack(ring[1]);
      ref_mesh.m_CapIndices.PushBack(ring[2]);
    }

    if (ref_mesh.m_CapIndices.GetCount() == uiFirstIndex)
    {
      ref_mesh.m_CapPoints.SetCount(uiFirstCapPoint);
      return false;
    }

    return true;
  }

  /// \brief Builds the extrusion walls for one contour, with normals that stay smooth across shallow angles.
  void BuildWalls(const Contour& contour, GlyphMesh& ref_mesh)
  {
    const plUInt32 uiCount = contour.m_Points.GetCount();

    plHybridArray<plVec2, 64> edgeNormals;
    plHybridArray<float, 64> edgeLengths;
    edgeNormals.SetCount(uiCount);
    edgeLengths.SetCount(uiCount);

    for (plUInt32 i = 0; i < uiCount; ++i)
    {
      const plVec2 vEdge = contour.m_Points[(i + 1) % uiCount] - contour.m_Points[i];

      edgeLengths[i] = vEdge.GetLength();

      // for a counter clockwise contour the solid is on the left of the edge, so the outward normal is on the right
      plVec2 vNormal(vEdge.y, -vEdge.x);
      if (vNormal.NormalizeIfNotZero(plVec2(1, 0), 0.0000001f).Failed())
      {
        vNormal.Set(1, 0);
      }

      edgeNormals[i] = vNormal;
    }

    // per vertex normals, but only where the two adjacent faces are nearly tangent
    plHybridArray<plVec2, 64> vertexNormals;
    plHybridArray<bool, 64> vertexIsSmooth;
    vertexNormals.SetCount(uiCount);
    vertexIsSmooth.SetCount(uiCount);

    for (plUInt32 i = 0; i < uiCount; ++i)
    {
      const plVec2& vPrev = edgeNormals[(i + uiCount - 1) % uiCount];
      const plVec2& vNext = edgeNormals[i];

      if (vPrev.Dot(vNext) > s_fSmoothNormalCosine)
      {
        plVec2 vAverage = vPrev + vNext;

        if (vAverage.NormalizeIfNotZero(vNext, 0.0000001f).Succeeded())
        {
          vertexNormals[i] = vAverage;
          vertexIsSmooth[i] = true;
          continue;
        }
      }

      vertexNormals[i] = vNext;
      vertexIsSmooth[i] = false;
    }

    float fDistance = 0.0f;

    for (plUInt32 i = 0; i < uiCount; ++i)
    {
      const plUInt32 uiNext = (i + 1) % uiCount;

      WallEdge& edge = ref_mesh.m_WallEdges.ExpandAndGetRef();
      edge.m_vPosA = contour.m_Points[i];
      edge.m_vPosB = contour.m_Points[uiNext];
      edge.m_vNormalA = vertexIsSmooth[i] ? vertexNormals[i] : edgeNormals[i];
      edge.m_vNormalB = vertexIsSmooth[uiNext] ? vertexNormals[uiNext] : edgeNormals[i];
      edge.m_fDistanceA = fDistance;
      edge.m_fDistanceB = fDistance + edgeLengths[i];

      fDistance = edge.m_fDistanceB;
    }
  }

  //////////////////////////////////////////////////////////////////////////

  struct PlacedGlyph
  {
    PL_DECLARE_POD_TYPE();

    plUInt32 m_uiGlyphIndex;
    plVec2 m_vOffset;
  };

  class TextMeshBuilder
  {
  public:
    ~TextMeshBuilder()
    {
      if (m_FtFace != nullptr)
        FT_Done_Face(m_FtFace);

      if (m_FtLibrary != nullptr)
        FT_Done_FreeType(m_FtLibrary);
    }

    plStatus Build(const plTextMeshOptions& options, const plGeometry::GeoOptions& geoOptions, plGeometry& ref_geom);

  private:
    plStatus LoadFont(const plTextMeshOptions& options);
    const GlyphMesh* GetGlyphMesh(plUInt32 uiGlyphIndex);
    plStatus LayoutText(const plTextMeshOptions& options);
    void EmitGeometry(const plTextMeshOptions& options, const plGeometry::GeoOptions& geoOptions, plGeometry& ref_geom) const;

    plDynamicArray<plUInt8> m_FontData;
    FT_Library m_FtLibrary = nullptr;
    FT_Face m_FtFace = nullptr;

    float m_fUnitScale = 1.0f; // font units -> em units
    plUInt32 m_uiCurveSegments = 8;

    plMap<plUInt32, GlyphMesh> m_GlyphMeshes;
    plDynamicArray<PlacedGlyph> m_PlacedGlyphs;

    plVec2 m_vBoundsMin = plVec2::MakeZero();
    plVec2 m_vBoundsMax = plVec2::MakeZero();
    plUInt32 m_uiFailedShapes = 0;
    plUInt32 m_uiSkippedHoles = 0;
  };

  plStatus TextMeshBuilder::LoadFont(const plTextMeshOptions& options)
  {
    plFileReader reader;
    if (reader.Open(options.m_sFontFile).Failed())
    {
      return plStatus(plFmt("Failed to open font file '{}'", options.m_sFontFile));
    }

    m_FontData.SetCountUninitialized(static_cast<plUInt32>(reader.GetFileSize()));
    reader.ReadBytes(m_FontData.GetData(), m_FontData.GetCount());

    if (FT_Init_FreeType(&m_FtLibrary) != 0)
    {
      return plStatus("Failed to initialize FreeType");
    }

    if (FT_New_Memory_Face(m_FtLibrary, m_FontData.GetData(), m_FontData.GetCount(), 0, &m_FtFace) != 0)
    {
      return plStatus(plFmt("Failed to load font face from '{}'", options.m_sFontFile));
    }

    if (!FT_IS_SCALABLE(m_FtFace) || m_FtFace->units_per_EM == 0)
    {
      return plStatus(plFmt("'{}' is not an outline font, only scalable fonts can be turned into a mesh", options.m_sFontFile));
    }

    m_fUnitScale = 1.0f / static_cast<float>(m_FtFace->units_per_EM);
    m_uiCurveSegments = options.m_uiCurveDetail == 0 ? 8 : plMath::Clamp<plUInt32>(options.m_uiCurveDetail, 1, 64);

    return plStatus(PL_SUCCESS);
  }

  const GlyphMesh* TextMeshBuilder::GetGlyphMesh(plUInt32 uiGlyphIndex)
  {
    if (auto it = m_GlyphMeshes.Find(uiGlyphIndex); it.IsValid())
    {
      return &it.Value();
    }

    GlyphMesh& mesh = m_GlyphMeshes[uiGlyphIndex];

    // FT_LOAD_NO_SCALE keeps the outline in font units, which is exactly what we want for a resolution independent mesh
    if (FT_Load_Glyph(m_FtFace, uiGlyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) != 0)
    {
      return &mesh;
    }

    mesh.m_fAdvance = static_cast<float>(m_FtFace->glyph->metrics.horiAdvance) * m_fUnitScale;

    if (m_FtFace->glyph->format != FT_GLYPH_FORMAT_OUTLINE || m_FtFace->glyph->outline.n_points == 0)
    {
      return &mesh; // whitespace, only the advance matters
    }

    plDynamicArray<Contour> contours;

    {
      DecomposeState state;
      state.m_pContours = &contours;
      state.m_fScale = m_fUnitScale;
      state.m_uiCurveSegments = m_uiCurveSegments;

      FT_Outline_Funcs funcs;
      funcs.move_to = &DecomposeMoveTo;
      funcs.line_to = &DecomposeLineTo;
      funcs.conic_to = &DecomposeConicTo;
      funcs.cubic_to = &DecomposeCubicTo;
      funcs.shift = 0;
      funcs.delta = 0;

      if (FT_Outline_Decompose(&m_FtFace->glyph->outline, &funcs, &state) != 0)
      {
        return &mesh;
      }

      state.FinishContour();
    }

    for (plUInt32 i = contours.GetCount(); i > 0; --i)
    {
      Contour& contour = contours[i - 1];
      contour.m_fSignedArea = ComputeSignedArea(contour.m_Points);

      if (plMath::Abs(contour.m_fSignedArea) < s_fMinContourArea)
      {
        contours.RemoveAtAndCopy(i - 1);
      }
    }

    if (contours.IsEmpty())
      return &mesh;

    plDynamicArray<plHybridArray<plUInt32, 4>> groups;
    GroupContours(contours, groups);

    // filled outlines counter clockwise, holes clockwise: then the same normal formula works for both
    for (Contour& contour : contours)
    {
      EnsureWinding(contour.m_Points, !contour.m_bIsHole);
    }

    for (const auto& group : groups)
    {
      if (!TriangulateShape(contours, group, mesh, m_uiSkippedHoles))
      {
        ++m_uiFailedShapes;
      }
    }

    for (const Contour& contour : contours)
    {
      BuildWalls(contour, mesh);
    }

    mesh.m_vBoundsMin = plVec2(plMath::MaxValue<float>(), plMath::MaxValue<float>());
    mesh.m_vBoundsMax = plVec2(-plMath::MaxValue<float>(), -plMath::MaxValue<float>());

    for (const Contour& contour : contours)
    {
      for (const plVec2& p : contour.m_Points)
      {
        mesh.m_vBoundsMin = plVec2(plMath::Min(mesh.m_vBoundsMin.x, p.x), plMath::Min(mesh.m_vBoundsMin.y, p.y));
        mesh.m_vBoundsMax = plVec2(plMath::Max(mesh.m_vBoundsMax.x, p.x), plMath::Max(mesh.m_vBoundsMax.y, p.y));
      }
    }

    mesh.m_bHasGeometry = !mesh.m_WallEdges.IsEmpty() || !mesh.m_CapIndices.IsEmpty();

    return &mesh;
  }

  plStatus TextMeshBuilder::LayoutText(const plTextMeshOptions& options)
  {
    // a literal backslash-n is treated as a line break, because the property grid only offers single line editing
    plStringBuilder sText = options.m_sText;
    sText.ReplaceAll("\\n", "\n");
    sText.ReplaceAll("\r", "");

    plHybridArray<plStringView, 8> lines;
    sText.Split(true, lines, "\n");

    if (lines.IsEmpty())
    {
      return plStatus("The text is empty");
    }

    const float fLineAdvance = static_cast<float>(m_FtFace->height) * m_fUnitScale * options.m_fLineSpacing;
    const float fAscender = static_cast<float>(m_FtFace->ascender) * m_fUnitScale;
    const float fDescender = static_cast<float>(m_FtFace->descender) * m_fUnitScale;
    const bool bHasKerning = FT_HAS_KERNING(m_FtFace) != 0;

    float fVerticalOffset = 0.0f;
    if (options.m_bCenterVertically)
    {
      const float fTop = fAscender;
      const float fBottom = -static_cast<float>(lines.GetCount() - 1) * fLineAdvance + fDescender;
      fVerticalOffset = -(fTop + fBottom) * 0.5f;
    }

    m_vBoundsMin = plVec2(plMath::MaxValue<float>(), plMath::MaxValue<float>());
    m_vBoundsMax = plVec2(-plMath::MaxValue<float>(), -plMath::MaxValue<float>());

    for (plUInt32 uiLine = 0; uiLine < lines.GetCount(); ++uiLine)
    {
      const plUInt32 uiFirstOfLine = m_PlacedGlyphs.GetCount();
      const float fBaseline = fVerticalOffset - static_cast<float>(uiLine) * fLineAdvance;

      float fPenX = 0.0f;
      plUInt32 uiPrevGlyph = 0;

      for (auto it = lines[uiLine].GetIteratorFront(); it.IsValid(); ++it)
      {
        if (m_PlacedGlyphs.GetCount() >= s_uiMaxGlyphs)
        {
          return plStatus(plFmt("The text is too long, at most {} characters can be turned into a mesh", s_uiMaxGlyphs));
        }

        const plUInt32 uiGlyphIndex = FT_Get_Char_Index(m_FtFace, it.GetCharacter());

        if (bHasKerning && uiPrevGlyph != 0 && uiGlyphIndex != 0)
        {
          FT_Vector kerning;
          if (FT_Get_Kerning(m_FtFace, uiPrevGlyph, uiGlyphIndex, FT_KERNING_UNSCALED, &kerning) == 0)
          {
            fPenX += static_cast<float>(kerning.x) * m_fUnitScale;
          }
        }

        const GlyphMesh* pGlyph = GetGlyphMesh(uiGlyphIndex);

        if (pGlyph->m_bHasGeometry)
        {
          PlacedGlyph& placed = m_PlacedGlyphs.ExpandAndGetRef();
          placed.m_uiGlyphIndex = uiGlyphIndex;
          placed.m_vOffset = plVec2(fPenX, fBaseline);
        }

        fPenX += pGlyph->m_fAdvance + options.m_fLetterSpacing;
        uiPrevGlyph = uiGlyphIndex;
      }

      // horizontal alignment shifts the glyphs of this line only, so multi line text lines up as expected
      float fAlignmentOffset = 0.0f;
      switch (options.m_Alignment)
      {
        case plTextMeshAlignment::Left:
          fAlignmentOffset = 0.0f;
          break;
        case plTextMeshAlignment::Center:
          fAlignmentOffset = -fPenX * 0.5f;
          break;
        case plTextMeshAlignment::Right:
          fAlignmentOffset = -fPenX;
          break;
      }

      for (plUInt32 i = uiFirstOfLine; i < m_PlacedGlyphs.GetCount(); ++i)
      {
        m_PlacedGlyphs[i].m_vOffset.x += fAlignmentOffset;

        const GlyphMesh& glyph = m_GlyphMeshes[m_PlacedGlyphs[i].m_uiGlyphIndex];
        const plVec2 vMin = glyph.m_vBoundsMin + m_PlacedGlyphs[i].m_vOffset;
        const plVec2 vMax = glyph.m_vBoundsMax + m_PlacedGlyphs[i].m_vOffset;

        m_vBoundsMin = plVec2(plMath::Min(m_vBoundsMin.x, vMin.x), plMath::Min(m_vBoundsMin.y, vMin.y));
        m_vBoundsMax = plVec2(plMath::Max(m_vBoundsMax.x, vMax.x), plMath::Max(m_vBoundsMax.y, vMax.y));
      }
    }

    if (m_PlacedGlyphs.IsEmpty())
    {
      return plStatus("The text does not contain any character that this font can render");
    }

    return plStatus(PL_SUCCESS);
  }

  void TextMeshBuilder::EmitGeometry(const plTextMeshOptions& options, const plGeometry::GeoOptions& geoOptions, plGeometry& ref_geom) const
  {
    // plGeometry::GeoOptions::IsFlipWindingNecessary() is not exported from Core, so do the same check here
    const bool bFlipWinding = geoOptions.m_Transform.GetRotationalPart().GetDeterminant() < 0.0f;
    const float fScale = options.m_fSize;
    const float fHalfDepth = plMath::Max(0.0f, options.m_fExtrude) * 0.5f;
    const bool bExtruded = fHalfDepth > 0.0f;

    // one uv scale for both axes, so the texture is never stretched
    const plVec2 vSize = m_vBoundsMax - m_vBoundsMin;
    const float fUvScale = 1.0f / plMath::Max(0.0001f, plMath::Max(vSize.x, vSize.y));

    plUInt32 tri[3];
    plUInt32 quad[4];

    for (const PlacedGlyph& placed : m_PlacedGlyphs)
    {
      const GlyphMesh& glyph = m_GlyphMeshes.Find(placed.m_uiGlyphIndex).Value();

      // the layout works in a 2D space with x running along the reading direction and y pointing up;
      // engine space wants that upright and facing forward, so x becomes +Y (an object's left, which is
      // the viewer's right when facing it), y becomes +Z and the extrusion runs along +X.
      // That mapping is a plain rotation, so triangle orientation carries over unchanged.
      auto toWorld = [&](const plVec2& vLocal, float fDepth) -> plVec3
      {
        const plVec2 v = vLocal + placed.m_vOffset;
        return plVec3(fDepth, v.x * fScale, v.y * fScale);
      };

      auto toCapUv = [&](const plVec2& vLocal) -> plVec2
      {
        const plVec2 v = vLocal + placed.m_vOffset - m_vBoundsMin;
        return plVec2(v.x * fUvScale, 1.0f - v.y * fUvScale);
      };

      // front and back cap
      {
        const plUInt32 uiFirstFront = ref_geom.GetVertices().GetCount();

        for (const plVec2& p : glyph.m_CapPoints)
        {
          ref_geom.AddVertex(geoOptions, toWorld(p, fHalfDepth), plVec3(1, 0, 0), toCapUv(p));
        }

        plUInt32 uiFirstBack = 0;
        if (bExtruded)
        {
          uiFirstBack = ref_geom.GetVertices().GetCount();

          for (const plVec2& p : glyph.m_CapPoints)
          {
            ref_geom.AddVertex(geoOptions, toWorld(p, -fHalfDepth), plVec3(-1, 0, 0), toCapUv(p));
          }
        }

        for (plUInt32 i = 0; i + 2 < glyph.m_CapIndices.GetCount(); i += 3)
        {
          tri[0] = uiFirstFront + glyph.m_CapIndices[i + 0];
          tri[1] = uiFirstFront + glyph.m_CapIndices[i + 1];
          tri[2] = uiFirstFront + glyph.m_CapIndices[i + 2];
          ref_geom.AddPolygon(tri, bFlipWinding);

          if (bExtruded)
          {
            tri[0] = uiFirstBack + glyph.m_CapIndices[i + 2];
            tri[1] = uiFirstBack + glyph.m_CapIndices[i + 1];
            tri[2] = uiFirstBack + glyph.m_CapIndices[i + 0];
            ref_geom.AddPolygon(tri, bFlipWinding);
          }
        }
      }

      if (!bExtruded)
        continue;

      // extrusion walls
      for (const WallEdge& edge : glyph.m_WallEdges)
      {
        const plVec3 vNormalA(0, edge.m_vNormalA.x, edge.m_vNormalA.y);
        const plVec3 vNormalB(0, edge.m_vNormalB.x, edge.m_vNormalB.y);

        const float fUA = edge.m_fDistanceA * fUvScale;
        const float fUB = edge.m_fDistanceB * fUvScale;

        // ordered so that the winding matches the outward normal
        quad[0] = ref_geom.AddVertex(geoOptions, toWorld(edge.m_vPosA, fHalfDepth), vNormalA, plVec2(fUA, 0.0f));
        quad[1] = ref_geom.AddVertex(geoOptions, toWorld(edge.m_vPosA, -fHalfDepth), vNormalA, plVec2(fUA, 1.0f));
        quad[2] = ref_geom.AddVertex(geoOptions, toWorld(edge.m_vPosB, -fHalfDepth), vNormalB, plVec2(fUB, 1.0f));
        quad[3] = ref_geom.AddVertex(geoOptions, toWorld(edge.m_vPosB, fHalfDepth), vNormalB, plVec2(fUB, 0.0f));

        ref_geom.AddPolygon(quad, bFlipWinding);
      }
    }
  }

  plStatus TextMeshBuilder::Build(const plTextMeshOptions& options, const plGeometry::GeoOptions& geoOptions, plGeometry& ref_geom)
  {
    PL_SUCCEED_OR_RETURN(LoadFont(options));
    PL_SUCCEED_OR_RETURN(LayoutText(options));

    EmitGeometry(options, geoOptions, ref_geom);

    if (m_uiFailedShapes > 0)
    {
      plLog::Warning("{} glyph shape(s) of '{}' could not be triangulated and were left out. The font may contain self intersecting outlines.", m_uiFailedShapes, options.m_sFontFile);
    }

    if (m_uiSkippedHoles > 0)
    {
      plLog::Warning("{} glyph hole(s) of '{}' could not be cut out and are filled instead. The font may let a hole touch its outline.", m_uiSkippedHoles, options.m_sFontFile);
    }

    return plStatus(PL_SUCCESS);
  }
} // namespace

plStatus plGenerateTextGeometry(const plTextMeshOptions& options, const plGeometry::GeoOptions& geoOptions, plGeometry& ref_geom)
{
  if (options.m_sFontFile.IsEmpty())
  {
    return plStatus("No font file specified");
  }

  if (options.m_sText.IsEmpty())
  {
    return plStatus("No text specified");
  }

  if (options.m_fSize <= 0.0f)
  {
    return plStatus("The text size must be greater than zero");
  }

  TextMeshBuilder builder;
  return builder.Build(options, geoOptions, ref_geom);
}
