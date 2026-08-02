#pragma once

#include <Core/ResourceManager/ResourceHandle.h>
#include <Core/World/World.h>
#include <GameComponentsPlugin/GameComponentsDLL.h>
#include <RendererCore/Components/RenderComponent.h>
#include <RendererCore/Pipeline/RenderData.h>

class plGeometry;
class plMeshResourceDescriptor;
struct plMsgExtractRenderData;
struct plMsgExtractGeometry;
struct plMsgSetMeshMaterial;
struct plMsgSetColor;
struct plMsgSetCustomData;

using plMeshResourceHandle = plTypedResourceHandle<class plMeshResource>;
using plMaterialResourceHandle = plTypedResourceHandle<class plMaterialResource>;

/// \brief Horizontal placement of a text mesh relative to the object's origin.
struct PL_GAMECOMPONENTS_DLL plTextMeshAlignment
{
  using StorageType = plInt8;

  enum Enum
  {
    Left,   ///< Each line starts at the origin
    Center, ///< Each line is centered on the origin
    Right,  ///< Each line ends at the origin

    Default = Center
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMECOMPONENTS_DLL, plTextMeshAlignment);

using plTextMeshComponentManager = plComponentManager<class plTextMeshComponent, plBlockStorageType::Compact>;

/// \brief Turns a string into solid, extruded 3D letters.
///
/// The text is typed directly on the component and the mesh is built from the font's glyph outlines,
/// so there is no asset to author or re-transform. Handy for signage, logos and showcase levels.
///
/// The letters stand upright (+Z is up) and face along the object's forward direction, so the text
/// reads correctly when looking at the front of the object.
///
/// \note Building the mesh is not free. Every change to the text or its layout rebuilds the geometry
/// on the spot, so this is meant for text that changes rarely, not for per frame updates.
class PL_GAMECOMPONENTS_DLL plTextMeshComponent : public plRenderComponent
{
  PL_DECLARE_COMPONENT_TYPE(plTextMeshComponent, plRenderComponent, plTextMeshComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnActivated() override;

  //////////////////////////////////////////////////////////////////////////
  // plRenderComponent

protected:
  virtual plResult GetLocalBounds(plBoundingBoxSphere& bounds, bool& bAlwaysVisible, plMsgUpdateLocalBounds& msg) override;
  void OnMsgExtractRenderData(plMsgExtractRenderData& msg) const;

  //////////////////////////////////////////////////////////////////////////
  // plTextMeshComponent

public:
  plTextMeshComponent();
  ~plTextMeshComponent();

  /// \brief The text to build. Type a backslash followed by n to start a new line.
  void SetText(const char* szText);                        // [ property ]
  const char* GetText() const { return m_sText; }          // [ property ]

  /// \brief A TrueType or OpenType font file to take the glyph outlines from.
  void SetFontFile(const char* szFile);                    // [ property ]
  const char* GetFontFile() const { return m_sFontFile; }  // [ property ]

  /// \brief Size of the font's em square in meters, roughly the height of a capital letter.
  void SetSize(float fSize);                               // [ property ]
  float GetSize() const { return m_fSize; }                // [ property ]

  /// \brief How thick the letters are. Zero produces flat, single sided text.
  void SetExtrude(float fExtrude);                         // [ property ]
  float GetExtrude() const { return m_fExtrude; }          // [ property ]

  /// \brief Additional space between letters, as a fraction of the text size.
  void SetLetterSpacing(float fSpacing);                          // [ property ]
  float GetLetterSpacing() const { return m_fLetterSpacing; }     // [ property ]

  /// \brief Multiplier for the line height that the font specifies.
  void SetLineSpacing(float fSpacing);                        // [ property ]
  float GetLineSpacing() const { return m_fLineSpacing; }     // [ property ]

  /// \brief Where each line sits relative to the object's origin.
  void SetAlignment(plEnum<plTextMeshAlignment> alignment);              // [ property ]
  plEnum<plTextMeshAlignment> GetAlignment() const { return m_Alignment; } // [ property ]

  /// \brief Centers the whole text block on the origin instead of putting the first baseline there.
  void SetCenterVertically(bool b);                                     // [ property ]
  bool GetCenterVertically() const { return m_bCenterVertically; }      // [ property ]

  /// \brief How many segments each curve of a glyph is flattened into. 0 uses a sensible default.
  void SetCurveDetail(plUInt16 uiDetail);                        // [ property ]
  plUInt16 GetCurveDetail() const { return m_uiCurveDetail; }    // [ property ]

  /// \brief An additional tint color passed to the renderer.
  void SetColor(const plColor& color);       // [ property ]
  const plColor& GetColor() const;           // [ property ]

  /// \brief An additional vec4 passed to the renderer that can be used by custom material shaders for effects.
  void SetCustomData(const plVec4& vData);   // [ property ]
  const plVec4& GetCustomData() const;       // [ property ]

  /// \brief The plMaterialResource file to use.
  void SetMaterialFile(const char* szFile);  // [ property ]
  const char* GetMaterialFile() const;       // [ property ]

  void SetMaterial(const plMaterialResourceHandle& hMaterial) { m_hMaterial = hMaterial; }
  plMaterialResourceHandle GetMaterial() const { return m_hMaterial; }

protected:
  void OnMsgSetMeshMaterial(plMsgSetMeshMaterial& ref_msg); // [ msg handler ]
  void OnMsgSetColor(plMsgSetColor& ref_msg);               // [ msg handler ]
  void OnMsgSetCustomData(plMsgSetCustomData& ref_msg);     // [ msg handler ]
  void OnMsgExtractGeometry(plMsgExtractGeometry& ref_msg) const;

  /// \brief Rebuilds the mesh, but only if one was already built. Called by every property setter.
  void InvalidateMesh();

  template <typename ResourceType>
  plTypedResourceHandle<ResourceType> GenerateMesh() const;

  plResult BuildGeometry(plGeometry& out_geom) const;
  void GenerateMeshName(plStringBuilder& out_sName) const;

  plString m_sText;
  plString m_sFontFile;
  float m_fSize = 1.0f;
  float m_fExtrude = 0.1f;
  float m_fLetterSpacing = 0.0f;
  float m_fLineSpacing = 1.0f;
  plUInt16 m_uiCurveDetail = 0;
  plEnum<plTextMeshAlignment> m_Alignment;
  bool m_bCenterVertically = true;

  plMaterialResourceHandle m_hMaterial;
  plColor m_Color = plColor::White;
  plVec4 m_vCustomData = plVec4(0, 1, 0, 1);

  plMeshResourceHandle m_hMesh;

  mutable plInstanceDataOffset m_InstanceDataOffset;
};
