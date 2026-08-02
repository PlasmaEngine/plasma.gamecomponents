#include <GameComponentsPlugin/GameComponentsPCH.h>

#include <GameComponentsPlugin/Effects/MeshDecalComponent.h>

#include <Core/Messages/SetColorMessage.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Foundation/Containers/ArrayMap.h>
#include <Foundation/SimdMath/SimdRandom.h>
#include <RendererCore/Decals/Implementation/DecalManager.h>
#include <RendererCore/Lights/LightComponent.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/Textures/Texture2DResource.h>

//////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_STATIC_REFLECTED_TYPE(plMeshDecalDescription, plNoBase, 1, plRTTIDefaultAllocator<plMeshDecalDescription>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Index", m_uiIndex)->AddAttributes(new plClampValueAttribute(0, 7)),
    PL_RESOURCE_MEMBER_PROPERTY("BaseColorTexture", m_hBaseColorTexture)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Texture_2D")),
  }
  PL_END_PROPERTIES;
}
PL_END_STATIC_REFLECTED_TYPE;
// clang-format on

plResult plMeshDecalDescription::Serialize(plStreamWriter& inout_stream) const
{
  inout_stream << m_uiIndex;
  inout_stream << m_hBaseColorTexture;

  return PL_SUCCESS;
}

plResult plMeshDecalDescription::Deserialize(plStreamReader& inout_stream)
{
  inout_stream >> m_uiIndex;
  inout_stream >> m_hBaseColorTexture;

  return PL_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plMeshDecalComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ARRAY_ACCESSOR_PROPERTY("Decals", Decals_GetCount, Decals_Get, Decals_Set, Decals_Insert, Decals_Remove),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("Effects"),
  }
  PL_END_ATTRIBUTES;
  PL_BEGIN_MESSAGEHANDLERS
  {
    PL_MESSAGE_HANDLER(plMsgExtractRenderData, OnMsgExtractRenderData),
  }
  PL_END_MESSAGEHANDLERS;
}
PL_END_COMPONENT_TYPE
// clang-format on

void plMeshDecalComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  plStreamWriter& s = inout_stream.GetStream();

  s.WriteArray(m_DecalDescs).AssertSuccess();
}

void plMeshDecalComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  const plUInt32 uiVersion = inout_stream.GetComponentTypeVersion(GetStaticRTTI());

  plStreamReader& s = inout_stream.GetStream();

  s.ReadArray(m_DecalDescs).AssertSuccess();
}

void plMeshDecalComponent::OnActivated()
{
  SUPER::OnActivated();

  UpdateDecals();
}

void plMeshDecalComponent::OnDeactivated()
{
  SUPER::OnDeactivated();

  DeleteDecals();
}

void plMeshDecalComponent::OnMsgExtractRenderData(plMsgExtractRenderData& msg) const
{
  if (msg.m_pView->GetCameraUsageHint() == plCameraUsageHint::Shadow)
    return;

  const float fScreenSpaceSize = plLightComponent::CalculateScreenSpaceSize(GetOwner()->GetGlobalBounds().GetSphere(), *msg.m_pView->GetCullingCamera());

  for (plDecalId decalId : m_DecalIds)
  {
    plDecalManager::MarkRuntimeDecalAsUsed(decalId, fScreenSpaceSize, msg.m_pView);
  }
}

plUInt32 plMeshDecalComponent::Decals_GetCount() const
{
  return m_DecalDescs.GetCount();
}

const plMeshDecalDescription& plMeshDecalComponent::Decals_Get(plUInt32 uiIndex) const
{
  return m_DecalDescs[uiIndex];
}

void plMeshDecalComponent::Decals_Set(plUInt32 uiIndex, const plMeshDecalDescription& desc)
{
  m_DecalDescs[uiIndex] = desc;

  UpdateDecals();
}

void plMeshDecalComponent::Decals_Insert(plUInt32 uiIndex, const plMeshDecalDescription& desc)
{
  m_DecalDescs.InsertAt(uiIndex, desc);

  UpdateDecals();
}

void plMeshDecalComponent::Decals_Remove(plUInt32 uiIndex)
{
  m_DecalDescs.RemoveAtAndCopy(uiIndex);

  UpdateDecals();
}

void plMeshDecalComponent::UpdateDecals()
{
  if (!IsActiveAndInitialized())
    return;

  DeleteDecals();

  plArrayMap<plUInt32, plTexture2DResourceHandle> indexToTexture(plFrameAllocator::GetCurrentAllocator());
  indexToTexture.Reserve(m_DecalDescs.GetCount());

  for (auto& desc : m_DecalDescs)
  {
    indexToTexture.Insert(desc.m_uiIndex, desc.m_hBaseColorTexture);
  }

  plUInt32 uiRandomSeed = GetOwner()->GetStableRandomSeed();
  int iRandomPos = 0;
  auto RandomIndex = [uiRandomSeed, &iRandomPos](plUInt32 uiMin, plUInt32 uiMax)
  {
    plUInt32 uiIndex = static_cast<plUInt32>(plSimdRandom::FloatMinMax(plSimdVec4i(iRandomPos), plSimdVec4f(float(uiMin)), plSimdVec4f(float(uiMax)), plSimdVec4u(uiRandomSeed)).x());
    iRandomPos++;
    return uiIndex;
  };

  plUInt16 decalIndices[8] = {};
  for (plUInt32 i = 0; i < 8; ++i)
  {
    decalIndices[i] = plSmallInvalidIndex;

    const plUInt32 uiLowerBound = indexToTexture.LowerBound(i);
    const plUInt32 uiUpperBound = plMath::Min(indexToTexture.UpperBound(i), indexToTexture.GetCount());

    if (uiLowerBound != plInvalidIndex && uiUpperBound > uiLowerBound)
    {
      const plUInt32 uiIndex = RandomIndex(uiLowerBound, uiUpperBound);
      auto& hTexture = indexToTexture.GetValue(uiIndex);
      if (hTexture.IsValid())
      {
        plDecalId decalId = plDecalManager::GetOrCreateRuntimeDecal(hTexture);
        decalIndices[i] = decalId.m_InstanceIndex;

        if (!m_DecalIds.Contains(decalId))
          m_DecalIds.PushBack(decalId);
      }
    }
  }

  const float* pDecalIndicesAsFloat = reinterpret_cast<const float*>(decalIndices);
  plMsgSetCustomData msg;
  msg.m_fData0 = pDecalIndicesAsFloat[0];
  msg.m_fData1 = pDecalIndicesAsFloat[1];
  msg.m_fData2 = pDecalIndicesAsFloat[2];
  msg.m_fData3 = pDecalIndicesAsFloat[3];

  GetOwner()->PostMessage(msg, plTime::MakeZero(), plObjectMsgQueueType::AfterInitialized);
}

void plMeshDecalComponent::DeleteDecals()
{
  for (plDecalId decalId : m_DecalIds)
  {
    plDecalManager::DeleteRuntimeDecal(decalId);
  }

  m_DecalIds.Clear();
}