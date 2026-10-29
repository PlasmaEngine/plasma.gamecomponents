#include <GameComponentsPlugin/GameComponentsPCH.h>

#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/Interfaces/WindWorldModule.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <GameComponentsPlugin/Physics/ClothSheetComponent.h>
#include <RendererCore/Material/MaterialResource.h>
#include <RendererCore/Meshes/CustomMeshComponent.h>
#include <RendererCore/Meshes/DynamicMeshBufferResource.h>
#include <RendererCore/Pipeline/RenderDataManager.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_BITFLAGS(plClothSheetFlags, 1)
  PL_ENUM_CONSTANT(plClothSheetFlags::FixedCornerTopLeft),
  PL_ENUM_CONSTANT(plClothSheetFlags::FixedCornerTopRight),
  PL_ENUM_CONSTANT(plClothSheetFlags::FixedCornerBottomRight),
  PL_ENUM_CONSTANT(plClothSheetFlags::FixedCornerBottomLeft),
  PL_ENUM_CONSTANT(plClothSheetFlags::FixedEdgeTop),
  PL_ENUM_CONSTANT(plClothSheetFlags::FixedEdgeRight),
  PL_ENUM_CONSTANT(plClothSheetFlags::FixedEdgeBottom),
  PL_ENUM_CONSTANT(plClothSheetFlags::FixedEdgeLeft),
PL_END_STATIC_REFLECTED_BITFLAGS;

PL_BEGIN_COMPONENT_TYPE(plClothSheetComponent, 1, plComponentMode::Static)
  {
    PL_BEGIN_PROPERTIES
    {
      PL_ACCESSOR_PROPERTY("Size", GetSize, SetSize)->AddAttributes(new plDefaultValueAttribute(plVec2(0.5f, 0.5f))),
      PL_ACCESSOR_PROPERTY("Slack", GetSlack, SetSlack)->AddAttributes(new plDefaultValueAttribute(plVec2(0.0f, 0.0f))),
      PL_ACCESSOR_PROPERTY("Segments", GetSegments, SetSegments)->AddAttributes(new plDefaultValueAttribute(plVec2U32(7, 7)), new plClampValueAttribute(plVec2U32(1, 1), plVec2U32(31, 31))),
      PL_MEMBER_PROPERTY("Damping", m_fDamping)->AddAttributes(new plDefaultValueAttribute(0.5f), new plClampValueAttribute(0.0f, 1.0f)),
      PL_MEMBER_PROPERTY("WindInfluence", m_fWindInfluence)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 10.0f)),
      PL_BITFLAGS_ACCESSOR_PROPERTY("Flags", plClothSheetFlags, GetFlags, SetFlags),
      PL_RESOURCE_MEMBER_PROPERTY("Material", m_hMaterial)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Material")),
      PL_MEMBER_PROPERTY("Color", m_Color)->AddAttributes(new plDefaultValueAttribute(plColor::White)),
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
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plClothSheetComponent::plClothSheetComponent() = default;
plClothSheetComponent::~plClothSheetComponent() = default;

void plClothSheetComponent::SetSize(plVec2 vVal)
{
  m_vSize = vVal;
  SetupCloth();
}

void plClothSheetComponent::SetSlack(plVec2 vVal)
{
  m_vSlack = vVal;
  SetupCloth();
}

void plClothSheetComponent::SetSegments(plVec2U32 vVal)
{
  m_vSegments = vVal;
  SetupCloth();
}

void plClothSheetComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_vSize;
  s << m_vSegments;
  s << m_vSlack;
  s << m_fWindInfluence;
  s << m_fDamping;
  s << m_Flags;
  s << m_hMaterial;
  s << m_Color;
}

void plClothSheetComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  // const plUInt32 uiVersion = inout_stream.GetComponentTypeVersion(GetStaticRTTI());
  auto& s = inout_stream.GetStream();

  s >> m_vSize;
  s >> m_vSegments;
  s >> m_vSlack;
  s >> m_fWindInfluence;
  s >> m_fDamping;
  s >> m_Flags;
  s >> m_hMaterial;
  s >> m_Color;
}

void plClothSheetComponent::OnActivated()
{
  SUPER::OnActivated();

  SetupCloth();
}

void plClothSheetComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  SetupCloth();
}

void plClothSheetComponent::OnDeactivated()
{
  plRenderDataManager* pRenderDataManager = GetWorld()->GetModule<plRenderDataManager>();
  pRenderDataManager->DeleteInstanceData(m_InstanceDataOffset);

  m_Simulator.m_Nodes.Clear();

  SUPER::OnDeactivated();
}

plResult plClothSheetComponent::GetLocalBounds(plBoundingBoxSphere& ref_bounds, bool& ref_bAlwaysVisible, plMsgUpdateLocalBounds& ref_msg)
{
  if (m_Bbox.IsValid())
  {
    ref_bounds.ExpandToInclude(plBoundingBoxSphere::MakeFromBox(m_Bbox));
  }
  else
  {
    plBoundingBox box = plBoundingBox::MakeInvalid();
    box.ExpandToInclude(plVec3::MakeZero());
    box.ExpandToInclude(plVec3(m_vSize.x, 0, -0.1f));
    box.ExpandToInclude(plVec3(0, m_vSize.y, +0.1f));
    box.ExpandToInclude(plVec3(m_vSize.x, m_vSize.y, 0));

    ref_bounds.ExpandToInclude(plBoundingBoxSphere::MakeFromBox(box));
  }

  return PL_SUCCESS;
}

void plClothSheetComponent::OnMsgExtractRenderData(plMsgExtractRenderData& msg) const
{
  if (!m_hDynamicMeshBuffer.IsValid())
    return;

  const bool bDynamic = GetOwner()->IsDynamic();
  auto hInstanceDataBuffer = msg.m_pRenderDataManager->GetOrCreateInstanceDataAndFill(*this, bDynamic, GetOwner()->GetGlobalTransform(), m_InstanceDataOffset, GetUniqueIdForRendering(), m_Color);

  plCustomMeshRenderData* pRenderData = msg.m_pRenderDataManager->CreateRenderDataForThisFrame<plCustomMeshRenderData>(GetOwner());
  {
    pRenderData->m_uiNumInstances = 1;
    pRenderData->m_DataOffsets.m_uiInstance = m_InstanceDataOffset.m_uiOffset;
    pRenderData->m_hInstanceDataBuffer = hInstanceDataBuffer;
    pRenderData->m_fSortingDepthOffset = 0.0f;

    pRenderData->m_hMaterial = m_hMaterial;
    pRenderData->m_hDynamicMeshBuffer = m_hDynamicMeshBuffer;
    pRenderData->m_uiFirstPrimitive = 0;
    pRenderData->m_uiNumPrimitives = m_vSegments.x * m_vSegments.y * 2;

#if PL_ENABLED(PL_COMPILE_FOR_DEVELOPMENT)
    pRenderData->m_FallbackGlobalBBox = GetOwner()->GetGlobalBounds().GetBox();
#endif

    pRenderData->FillSortingKey();
  }

  plRenderData::Category category = plDefaultRenderDataCategories::LitOpaque;
  bool bDontCacheYet = true;

  if (m_hMaterial.IsValid())
  {
    plResourceLock<plMaterialResource> pMaterial(m_hMaterial, plResourceAcquireMode::AllowLoadingFallback);
    category = pMaterial->GetRenderDataCategory();
    bDontCacheYet = pMaterial.GetAcquireResult() == plResourceAcquireResult::LoadingFallback;
  }

  // Acquiring the material above assigns its ID. Do not cache the default index from an unloaded material.
  pRenderData->m_DataOffsets.m_uiMaterial = plPackMaterialDataOffset(m_hMaterial, 0);
  msg.AddRenderData(pRenderData, category, bDontCacheYet ? plRenderData::Caching::Never : plRenderData::Caching::IfStatic);
}

void plClothSheetComponent::SetFlags(plBitflags<plClothSheetFlags> flags)
{
  m_Flags = flags;
  SetupCloth();
}

void plClothSheetComponent::Update()
{
  if (m_Simulator.m_Nodes.IsEmpty() || GetOwner()->GetVisibilityState() == plVisibilityState::Invisible)
    return;

  {
    plVec3 acc = -GetOwner()->GetLinearVelocity();

    if (const plPhysicsWorldModuleInterface* pModule = GetWorld()->GetModuleReadOnly<plPhysicsWorldModuleInterface>())
    {
      acc += pModule->GetGravity();
    }
    else
    {
      acc += plVec3(0, 0, -9.81f);
    }

    if (m_fWindInfluence > 0.0f)
    {
      if (const plWindWorldModuleInterface* pWind = GetWorld()->GetModuleReadOnly<plWindWorldModuleInterface>())
      {
        plVec3 ropeDir(0, 0, 1);

        // take the position of the center cloth node to sample the wind
        const plVec3 vSampleWindPos = GetOwner()->GetGlobalTransform().TransformPosition(plSimdConversion::ToVec3(m_Simulator.m_Nodes[m_Simulator.m_uiWidth * (m_Simulator.m_uiHeight / 2) + m_Simulator.m_uiWidth / 2].m_vPosition));

        const plVec3 vWind = pWind->GetWindAt(vSampleWindPos) * m_fWindInfluence;

        acc += vWind;
        acc += pWind->ComputeWindFlutter(vWind, ropeDir, 0.5f, GetOwner()->GetStableRandomSeed());
      }
    }

    // rotate the acceleration vector into the local simulation space
    acc = GetOwner()->GetGlobalRotation().GetInverse() * acc;

    if (m_Simulator.m_vAcceleration != acc)
    {
      m_Simulator.m_vAcceleration = acc;
      m_uiSleepCounter = 0;
    }
  }

  if (m_uiSleepCounter <= 10)
  {
    m_Simulator.m_fDampingFactor = plMath::Lerp(1.0f, 0.97f, m_fDamping);

    m_Simulator.SimulateCloth(GetWorld()->GetClock().GetTimeDiff());

    auto prevBbox = m_Bbox;
    m_Bbox.ExpandToInclude(plSimdConversion::ToVec3(m_Simulator.m_Nodes[0].m_vPosition));
    m_Bbox.ExpandToInclude(plSimdConversion::ToVec3(m_Simulator.m_Nodes[m_Simulator.m_uiWidth - 1].m_vPosition));
    m_Bbox.ExpandToInclude(plSimdConversion::ToVec3(m_Simulator.m_Nodes[((m_Simulator.m_uiHeight - 1) * m_Simulator.m_uiWidth)].m_vPosition));
    m_Bbox.ExpandToInclude(plSimdConversion::ToVec3(m_Simulator.m_Nodes.PeekBack().m_vPosition));

    if (prevBbox != m_Bbox)
    {
      SetUserFlag(0, true); // flag 0 => requires local bounds update

      // can't call this here in the async phase
      // TriggerLocalBoundsUpdate();
    }

    UpdateClothMesh();

    ++m_uiCheckEquilibriumCounter;
    if (m_uiCheckEquilibriumCounter > 64)
    {
      m_uiCheckEquilibriumCounter = 0;

      if (m_Simulator.HasEquilibrium(0.01f))
      {
        ++m_uiSleepCounter;
      }
      else
      {
        m_uiSleepCounter = 0;
      }
    }
  }
}

void plClothSheetComponent::UpdateClothMesh()
{
  plResourceLock<plDynamicMeshBufferResource> pDynamicMeshBuffer(m_hDynamicMeshBuffer, plResourceAcquireMode::BlockTillLoaded);

  auto nodes = m_Simulator.m_Nodes.GetArrayPtr();
  auto positions = pDynamicMeshBuffer->AccessPositionData();

  const plVec2U32 vNumVertices = m_vSegments + plVec2U32(1);

  plUInt32 vidx = 0;
  for (plUInt32 y = 0; y < vNumVertices.y; ++y)
  {
    for (plUInt32 x = 0; x < vNumVertices.x; ++x, ++vidx)
    {
      positions[vidx] = plSimdConversion::ToVec3(nodes[vidx].m_vPosition);
    }
  }

  plDynamicMeshBufferResource::CalculateGridNormalAndTangents(pDynamicMeshBuffer.GetPointerNonConst(), vNumVertices);
}

void plClothSheetComponent::SetupCloth()
{
  m_Bbox = plBoundingBox::MakeInvalid();

  if (IsActiveAndSimulating())
  {
    m_uiSleepCounter = 0;

    m_Simulator.m_uiWidth = static_cast<plUInt8>(m_vSegments.x + 1);
    m_Simulator.m_uiHeight = static_cast<plUInt8>(m_vSegments.y + 1);
    m_Simulator.m_vAcceleration.Set(0, 0, -10);
    m_Simulator.m_vSegmentLength = m_vSize.CompMul(plVec2(1.0f) + m_vSlack);
    m_Simulator.m_vSegmentLength.x /= (float)m_vSegments.x;
    m_Simulator.m_vSegmentLength.y /= (float)m_vSegments.y;
    m_Simulator.m_Nodes.Clear();
    m_Simulator.m_Nodes.SetCount(m_Simulator.m_uiWidth * m_Simulator.m_uiHeight);

    const plVec3 dirX = plVec3(1, 0, 0);
    const plVec3 dirY = plVec3(0, 1, 0);

    plVec2 dist = m_vSize;
    dist.x /= (float)m_vSegments.x;
    dist.y /= (float)m_vSegments.y;

    for (plUInt32 y = 0; y < m_Simulator.m_uiHeight; ++y)
    {
      for (plUInt32 x = 0; x < m_Simulator.m_uiWidth; ++x)
      {
        const plUInt32 idx = (y * m_Simulator.m_uiWidth) + x;

        m_Simulator.m_Nodes[idx].m_vPosition = plSimdConversion::ToVec3(x * dist.x * dirX + y * dist.y * dirY);
        m_Simulator.m_Nodes[idx].m_vPreviousPosition = m_Simulator.m_Nodes[idx].m_vPosition;
      }
    }

    if (m_Flags.IsSet(plClothSheetFlags::FixedCornerTopLeft))
      m_Simulator.m_Nodes[0].m_bFixed = true;

    if (m_Flags.IsSet(plClothSheetFlags::FixedCornerTopRight))
      m_Simulator.m_Nodes[m_Simulator.m_uiWidth - 1].m_bFixed = true;

    if (m_Flags.IsSet(plClothSheetFlags::FixedCornerBottomRight))
      m_Simulator.m_Nodes[m_Simulator.m_uiWidth * m_Simulator.m_uiHeight - 1].m_bFixed = true;

    if (m_Flags.IsSet(plClothSheetFlags::FixedCornerBottomLeft))
      m_Simulator.m_Nodes[m_Simulator.m_uiWidth * (m_Simulator.m_uiHeight - 1)].m_bFixed = true;

    if (m_Flags.IsSet(plClothSheetFlags::FixedEdgeTop))
    {
      for (plUInt32 x = 0; x < m_Simulator.m_uiWidth; ++x)
      {
        const plUInt32 idx = (0 * m_Simulator.m_uiWidth) + x;

        m_Simulator.m_Nodes[idx].m_bFixed = true;
      }
    }

    if (m_Flags.IsSet(plClothSheetFlags::FixedEdgeRight))
    {
      for (plUInt32 y = 0; y < m_Simulator.m_uiHeight; ++y)
      {
        const plUInt32 idx = (y * m_Simulator.m_uiWidth) + (m_Simulator.m_uiWidth - 1);

        m_Simulator.m_Nodes[idx].m_bFixed = true;
      }
    }

    if (m_Flags.IsSet(plClothSheetFlags::FixedEdgeBottom))
    {
      for (plUInt32 x = 0; x < m_Simulator.m_uiWidth; ++x)
      {
        const plUInt32 idx = ((m_Simulator.m_uiHeight - 1) * m_Simulator.m_uiWidth) + x;

        m_Simulator.m_Nodes[idx].m_bFixed = true;
      }
    }

    if (m_Flags.IsSet(plClothSheetFlags::FixedEdgeLeft))
    {
      for (plUInt32 y = 0; y < m_Simulator.m_uiHeight; ++y)
      {
        const plUInt32 idx = (y * m_Simulator.m_uiWidth) + 0;

        m_Simulator.m_Nodes[idx].m_bFixed = true;
      }
    }
  }

  if (IsActiveAndInitialized() && m_vSize.x > 0 && m_vSize.y > 0 && m_vSegments.x > 0 && m_vSegments.y > 0)
  {
    plStringBuilder sResourceName;
    sResourceName.SetFormat("ClothSheet_{}_{}x{}_{}x{}", plArgP(this), m_vSize.x, m_vSize.y, m_vSegments.x, m_vSegments.y);

    m_hDynamicMeshBuffer = plResourceManager::GetExistingResource<plDynamicMeshBufferResource>(sResourceName);

    if (!m_hDynamicMeshBuffer.IsValid())
    {
      plDynamicMeshBufferResourceDescriptor desc;
      desc.m_uiMaxVertices = (m_vSegments.x + 1) * (m_vSegments.y + 1);
      desc.m_IndexType = plGALIndexType::UShort;
      desc.m_uiMaxPrimitives = m_vSegments.x * m_vSegments.y * 2;

      m_hDynamicMeshBuffer = plResourceManager::GetOrCreateResource<plDynamicMeshBufferResource>(sResourceName, std::move(desc));
    }

    plResourceLock<plDynamicMeshBufferResource> pDynamicMeshBuffer(m_hDynamicMeshBuffer, plResourceAcquireMode::BlockTillLoaded);
    plDynamicMeshBufferResource::CreateGridXY(pDynamicMeshBuffer.GetPointerNonConst(), m_vSize, m_vSegments + plVec2U32(1));
  }

  TriggerLocalBoundsUpdate();
}

//////////////////////////////////////////////////////////////////////////

plClothSheetComponentManager::plClothSheetComponentManager(plWorld* pWorld)
  : plComponentManager(pWorld)
{
}

plClothSheetComponentManager::~plClothSheetComponentManager() = default;

void plClothSheetComponentManager::Initialize()
{
  SUPER::Initialize();

  {
    auto desc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plClothSheetComponentManager::Update, this);
    desc.m_Phase = plWorldUpdatePhase::Async;
    desc.m_bOnlyUpdateWhenSimulating = true;
    desc.m_uiGranularity = 2;

    this->RegisterUpdateFunction(desc);
  }

  {
    auto desc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plClothSheetComponentManager::UpdateBounds, this);
    desc.m_Phase = plWorldUpdatePhase::PostAsync;
    desc.m_bOnlyUpdateWhenSimulating = true;

    this->RegisterUpdateFunction(desc);
  }
}

void plClothSheetComponentManager::Update(const plWorldModule::UpdateContext& context)
{
  for (auto it = this->m_ComponentStorage.GetIterator(context.m_uiFirstComponentIndex, context.m_uiComponentCount); it.IsValid(); ++it)
  {
    if (it->IsActiveAndInitialized())
    {
      it->Update();
    }
  }
}

void plClothSheetComponentManager::UpdateBounds(const plWorldModule::UpdateContext& context)
{
  for (auto it = this->m_ComponentStorage.GetIterator(context.m_uiFirstComponentIndex, context.m_uiComponentCount); it.IsValid(); ++it)
  {
    if (it->IsActiveAndInitialized() && it->GetUserFlag(0))
    {
      it->TriggerLocalBoundsUpdate();

      // reset update bounds flag
      it->SetUserFlag(0, false);
    }
  }
}


PL_STATICLINK_FILE(GameComponentsPlugin, GameComponentsPlugin_Physics_Implementation_ClothSheetComponent);
