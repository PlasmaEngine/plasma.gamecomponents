#include <GameComponentsPlugin/GameComponentsPCH.h>

#include <Core/Messages/SetColorMessage.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <GameComponentsPlugin/Text/Implementation/TextMeshGenerator.h>
#include <GameComponentsPlugin/Text/TextMeshComponent.h>
#include <RendererCore/Meshes/CpuMeshResource.h>
#include <RendererCore/Meshes/MeshComponent.h>
#include <RendererCore/Meshes/MeshResource.h>
#include <RendererCore/Pipeline/RenderDataManager.h>
#include <RendererCore/Utils/WorldGeoExtractionUtil.h>

// Data/Base/Materials/Common/Pattern.plMaterialAsset
static constexpr plStringView s_sDefaultMaterial = "{ 1c47ee4c-0379-4280-85f5-b8cda61941d2 }"_plsv;

// a font that ships with the engine, so that a freshly added component shows something right away
static constexpr plStringView s_sDefaultFont = "UI/Theme/Fonts/PT Root UI_Bold.otf"_plsv;

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plTextMeshAlignment, 1)
  PL_ENUM_CONSTANTS(plTextMeshAlignment::Left, plTextMeshAlignment::Center, plTextMeshAlignment::Right)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_COMPONENT_TYPE(plTextMeshComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Text", GetText, SetText)->AddAttributes(new plDefaultValueAttribute(plStringView("Text"))),
    PL_ACCESSOR_PROPERTY("Font", GetFontFile, SetFontFile)->AddAttributes(new plFileBrowserAttribute("Select font file", "*.ttf;*.otf"), new plDefaultValueAttribute(plStringView("UI/Theme/Fonts/PT Root UI_Bold.otf"))),
    PL_ACCESSOR_PROPERTY("Material", GetMaterialFile, SetMaterialFile)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Material")),
    PL_ACCESSOR_PROPERTY("Size", GetSize, SetSize)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.01f, 50.0f)),
    PL_ACCESSOR_PROPERTY("Extrude", GetExtrude, SetExtrude)->AddAttributes(new plDefaultValueAttribute(0.1f), new plClampValueAttribute(0.0f, 10.0f)),
    PL_ENUM_ACCESSOR_PROPERTY("Alignment", plTextMeshAlignment, GetAlignment, SetAlignment),
    PL_ACCESSOR_PROPERTY("CenterVertically", GetCenterVertically, SetCenterVertically)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_ACCESSOR_PROPERTY("LetterSpacing", GetLetterSpacing, SetLetterSpacing)->AddAttributes(new plDefaultValueAttribute(0.0f), new plClampValueAttribute(-0.5f, 2.0f)),
    PL_ACCESSOR_PROPERTY("LineSpacing", GetLineSpacing, SetLineSpacing)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 10.0f)),
    PL_ACCESSOR_PROPERTY("CurveDetail", GetCurveDetail, SetCurveDetail)->AddAttributes(new plDefaultValueAttribute(0), new plClampValueAttribute(0, 64)),
    PL_ACCESSOR_PROPERTY("Color", GetColor, SetColor)->AddAttributes(new plExposeColorAlphaAttribute()),
    PL_ACCESSOR_PROPERTY("CustomData", GetCustomData, SetCustomData)->AddAttributes(new plDefaultValueAttribute(plVec4(0, 1, 0, 1))),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("Construction"),
  }
  PL_END_ATTRIBUTES;
  PL_BEGIN_MESSAGEHANDLERS
  {
    PL_MESSAGE_HANDLER(plMsgExtractRenderData, OnMsgExtractRenderData),
    PL_MESSAGE_HANDLER(plMsgExtractGeometry, OnMsgExtractGeometry),
    PL_MESSAGE_HANDLER(plMsgSetMeshMaterial, OnMsgSetMeshMaterial),
    PL_MESSAGE_HANDLER(plMsgSetColor, OnMsgSetColor),
    PL_MESSAGE_HANDLER(plMsgSetCustomData, OnMsgSetCustomData),
  }
  PL_END_MESSAGEHANDLERS;
}
PL_END_COMPONENT_TYPE;
// clang-format on

plTextMeshComponent::plTextMeshComponent()
{
  m_sText = "Text";
  m_sFontFile = s_sDefaultFont;
}

plTextMeshComponent::~plTextMeshComponent() = default;

void plTextMeshComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  plStreamWriter& s = inout_stream.GetStream();

  s << m_sText;
  s << m_sFontFile;
  s << m_fSize;
  s << m_fExtrude;
  s << m_fLetterSpacing;
  s << m_fLineSpacing;
  s << m_uiCurveDetail;
  s << m_Alignment;
  s << m_bCenterVertically;
  s << m_hMaterial;
  s << m_Color;
  s << m_vCustomData;
}

void plTextMeshComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  plStreamReader& s = inout_stream.GetStream();

  s >> m_sText;
  s >> m_sFontFile;
  s >> m_fSize;
  s >> m_fExtrude;
  s >> m_fLetterSpacing;
  s >> m_fLineSpacing;
  s >> m_uiCurveDetail;
  s >> m_Alignment;
  s >> m_bCenterVertically;
  s >> m_hMaterial;
  s >> m_Color;
  s >> m_vCustomData;
}

void plTextMeshComponent::OnActivated()
{
  if (!m_hMesh.IsValid())
  {
    m_hMesh = GenerateMesh<plMeshResource>();
  }

  if (!m_hMaterial.IsValid())
  {
    SetMaterialFile(s_sDefaultMaterial.GetStartPointer());
  }

  // the mesh has to exist before the base class computes the bounds
  SUPER::OnActivated();
}

plResult plTextMeshComponent::GetLocalBounds(plBoundingBoxSphere& bounds, bool& bAlwaysVisible, plMsgUpdateLocalBounds& msg)
{
  if (!m_hMesh.IsValid())
    return PL_FAILURE;

  plResourceLock<plMeshResource> pMesh(m_hMesh, plResourceAcquireMode::AllowLoadingFallback);
  bounds = pMesh->GetBounds();

  return PL_SUCCESS;
}

void plTextMeshComponent::OnMsgExtractRenderData(plMsgExtractRenderData& msg) const
{
  if (!m_hMesh.IsValid())
    return;

  const bool bDynamic = GetOwner()->IsDynamic();
  auto hInstanceDataBuffer = msg.m_pRenderDataManager->GetOrCreateInstanceDataAndFill(*this, bDynamic, GetOwner()->GetGlobalTransform(), m_InstanceDataOffset, GetUniqueIdForRendering(), m_Color, m_vCustomData);

  plResourceLock<plMeshResource> pMesh(m_hMesh, plResourceAcquireMode::AllowLoadingFallback);
  plArrayPtr<const plMeshResourceDescriptor::SubMesh> parts = pMesh->GetSubMeshes();

  for (plUInt32 uiPartIndex = 0; uiPartIndex < parts.GetCount(); ++uiPartIndex)
  {
    const plUInt32 uiMaterialIndex = parts[uiPartIndex].m_uiMaterialIndex;
    plMaterialResourceHandle hMaterial = m_hMaterial.IsValid() ? m_hMaterial : pMesh->GetMaterials()[uiMaterialIndex];

    plMeshRenderData* pRenderData = msg.m_pRenderDataManager->CreateRenderDataForThisFrame<plMeshRenderData>(GetOwner());
    pRenderData->SetFallbackGlobalBounds(GetOwner()->GetGlobalBounds().GetBox());
    pRenderData->Fill(m_InstanceDataOffset, hInstanceDataBuffer, hMaterial, m_hMesh, uiMaterialIndex, uiPartIndex);

    bool bDontCacheYet = false;

    plRenderData::Category category = plMaterialResource::GetRenderDataCategory(hMaterial, &bDontCacheYet);

    msg.AddRenderData(pRenderData, category, bDontCacheYet ? plRenderData::Caching::Never : plRenderData::Caching::IfStatic);
  }
}

void plTextMeshComponent::OnMsgExtractGeometry(plMsgExtractGeometry& ref_msg) const
{
  // letters make poor colliders and navmesh obstacles, so only hand out the render geometry
  if (ref_msg.m_Mode != plWorldGeoExtractionUtil::ExtractionMode::RenderMesh)
    return;

  plCpuMeshResourceHandle hMesh = GenerateMesh<plCpuMeshResource>();

  if (hMesh.IsValid())
  {
    ref_msg.AddMeshObject(GetOwner()->GetGlobalTransform(), hMesh);
  }
}

void plTextMeshComponent::SetText(const char* szText)
{
  m_sText = szText;
  InvalidateMesh();
}

void plTextMeshComponent::SetFontFile(const char* szFile)
{
  m_sFontFile = szFile;
  InvalidateMesh();
}

void plTextMeshComponent::SetSize(float fSize)
{
  m_fSize = fSize;
  InvalidateMesh();
}

void plTextMeshComponent::SetExtrude(float fExtrude)
{
  m_fExtrude = fExtrude;
  InvalidateMesh();
}

void plTextMeshComponent::SetLetterSpacing(float fSpacing)
{
  m_fLetterSpacing = fSpacing;
  InvalidateMesh();
}

void plTextMeshComponent::SetLineSpacing(float fSpacing)
{
  m_fLineSpacing = fSpacing;
  InvalidateMesh();
}

void plTextMeshComponent::SetAlignment(plEnum<plTextMeshAlignment> alignment)
{
  m_Alignment = alignment;
  InvalidateMesh();
}

void plTextMeshComponent::SetCenterVertically(bool b)
{
  m_bCenterVertically = b;
  InvalidateMesh();
}

void plTextMeshComponent::SetCurveDetail(plUInt16 uiDetail)
{
  m_uiCurveDetail = uiDetail;
  InvalidateMesh();
}

void plTextMeshComponent::SetColor(const plColor& color)
{
  m_Color = color;

  InvalidateCachedRenderData();
}

const plColor& plTextMeshComponent::GetColor() const
{
  return m_Color;
}

void plTextMeshComponent::SetCustomData(const plVec4& vData)
{
  m_vCustomData = vData;

  InvalidateCachedRenderData();
}

const plVec4& plTextMeshComponent::GetCustomData() const
{
  return m_vCustomData;
}

void plTextMeshComponent::SetMaterialFile(const char* szFile)
{
  if (!plStringUtils::IsNullOrEmpty(szFile))
  {
    m_hMaterial = plResourceManager::LoadResource<plMaterialResource>(szFile);
  }
  else
  {
    m_hMaterial.Invalidate();
  }

  InvalidateCachedRenderData();
}

const char* plTextMeshComponent::GetMaterialFile() const
{
  if (!m_hMaterial.IsValid())
    return "";

  return m_hMaterial.GetResourceID();
}

void plTextMeshComponent::OnMsgSetMeshMaterial(plMsgSetMeshMaterial& ref_msg)
{
  SetMaterial(ref_msg.m_hMaterial);
}

void plTextMeshComponent::OnMsgSetColor(plMsgSetColor& ref_msg)
{
  ref_msg.ModifyColor(m_Color);

  InvalidateCachedRenderData();
}

void plTextMeshComponent::OnMsgSetCustomData(plMsgSetCustomData& ref_msg)
{
  m_vCustomData.Set(ref_msg.m_fData0, ref_msg.m_fData1, ref_msg.m_fData2, ref_msg.m_fData3);

  InvalidateCachedRenderData();
}

void plTextMeshComponent::InvalidateMesh()
{
  // before activation there is nothing to update, OnActivated() builds the first mesh.
  // Checking the handle instead would never recover from a failed build, e.g. from no font being picked yet.
  if (!IsActiveAndInitialized())
    return;

  m_hMesh.Invalidate();
  m_hMesh = GenerateMesh<plMeshResource>();

  TriggerLocalBoundsUpdate();
  InvalidateCachedRenderData();
}

plResult plTextMeshComponent::BuildGeometry(plGeometry& out_geom) const
{
  plTextMeshOptions opt;
  opt.m_sFontFile = m_sFontFile;
  opt.m_sText = m_sText;
  opt.m_fSize = m_fSize;
  opt.m_fExtrude = m_fExtrude;
  opt.m_fLetterSpacing = m_fLetterSpacing;
  opt.m_fLineSpacing = m_fLineSpacing;
  opt.m_uiCurveDetail = m_uiCurveDetail;
  opt.m_Alignment = m_Alignment;
  opt.m_bCenterVertically = m_bCenterVertically;

  plGeometry::GeoOptions geoOpt;

  if (plStatus res = plGenerateTextGeometry(opt, geoOpt, out_geom); res.Failed())
  {
    plLog::Warning("Text mesh of object '{}' could not be built: {}", GetOwner()->GetName(), res.m_sMessage);
    return PL_FAILURE;
  }

  out_geom.TriangulatePolygons(4);
  out_geom.ComputeTangents();

  return PL_SUCCESS;
}

void plTextMeshComponent::GenerateMeshName(plStringBuilder& out_sName) const
{
  // identical settings share one mesh, so a wall of repeated signs only builds the geometry once
  plStringBuilder sKey;
  sKey.SetFormat("{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|{8}", m_sFontFile, m_sText, m_fSize, m_fExtrude, m_fLetterSpacing, m_fLineSpacing, m_uiCurveDetail, m_Alignment.GetValue(), m_bCenterVertically);

  out_sName.SetFormat("TextMesh:{0}:{1}", plPathUtils::GetFileName(m_sFontFile), plArgU(plHashingUtils::StringHashTo32(plHashingUtils::StringHash(sKey.GetView())), 8, true, 16));
}

template <typename ResourceType>
plTypedResourceHandle<ResourceType> plTextMeshComponent::GenerateMesh() const
{
  if (m_sFontFile.IsEmpty() || m_sText.IsEmpty())
    return plTypedResourceHandle<ResourceType>();

  plStringBuilder sResourceName;
  GenerateMeshName(sResourceName);

  plTypedResourceHandle<ResourceType> hExisting = plResourceManager::GetExistingResource<ResourceType>(sResourceName);
  if (hExisting.IsValid())
    return hExisting;

  plGeometry geom;
  if (BuildGeometry(geom).Failed())
    return plTypedResourceHandle<ResourceType>();

  plMeshResourceDescriptor desc;
  desc.SetMaterial(0, s_sDefaultMaterial);

  desc.MeshBufferDesc().AddCommonStreams();
  desc.MeshBufferDesc().AllocateStreamsFromGeometry(geom, plGALPrimitiveTopology::Triangles);

  desc.AddSubMesh(desc.MeshBufferDesc().GetPrimitiveCount(), 0, 0);
  desc.ComputeBounds();

  return plResourceManager::GetOrCreateResource<ResourceType>(sResourceName, std::move(desc), sResourceName);
}
