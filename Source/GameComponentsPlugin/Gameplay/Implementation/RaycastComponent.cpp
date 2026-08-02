
#include <GameComponentsPlugin/GameComponentsPCH.h>

#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/Messages/CommonMessages.h>
#include <Core/Messages/TriggerMessage.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <GameComponentsPlugin/Gameplay/RaycastComponent.h>
#include <RendererCore/Debug/DebugRenderer.h>

plRaycastComponentManager::plRaycastComponentManager(plWorld* pWorld)
  : SUPER(pWorld)
{
}

void plRaycastComponentManager::Initialize()
{
  // we want to do the raycast as late as possible, ie. after animated objects and characters moved
  // such that we get the latest position that is in sync with those animated objects
  // therefore we move the update into the post async phase and set a low priority (low = updated late)
  // we DO NOT want to use post transform update, because when we move the target object
  // child objects of the target node should still get the full global transform update within this frame

  auto desc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plRaycastComponentManager::Update, this);
  desc.m_bOnlyUpdateWhenSimulating = true;
  desc.m_Phase = UpdateFunctionDesc::Phase::PostAsync;
  desc.m_fPriority = -1000;

  this->RegisterUpdateFunction(desc);
}

void plRaycastComponentManager::Update(const plWorldModule::UpdateContext& context)
{
  for (auto it = this->m_ComponentStorage.GetIterator(context.m_uiFirstComponentIndex, context.m_uiComponentCount); it.IsValid(); ++it)
  {
    ComponentType* pComponent = it;
    if (pComponent->IsActiveAndInitialized())
    {
      pComponent->Update();
    }
  }
}

////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plRaycastComponent, 4, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("MaxDistance", m_fMaxDistance)->AddAttributes(new plDefaultValueAttribute(100.0f), new plClampValueAttribute(0.0f, plVariant())),
    PL_MEMBER_PROPERTY("DisableTargetObjectOnNoHit", m_bDisableTargetObjectOnNoHit),
    PL_ACCESSOR_PROPERTY("RaycastEndObject", DummyGetter, SetRaycastEndObject)->AddAttributes(new plGameObjectReferenceAttribute()),
    PL_MEMBER_PROPERTY("ForceTargetParentless", m_bForceTargetParentless),
    PL_BITFLAGS_MEMBER_PROPERTY("ShapeTypesToHit", plPhysicsShapeType, m_ShapeTypesToHit)->AddAttributes(new plDefaultValueAttribute(plVariant(plPhysicsShapeType::Default & ~(plPhysicsShapeType::Trigger)))),
    PL_MEMBER_PROPERTY("CollisionLayerEndPoint", m_uiCollisionLayerEndPoint)->AddAttributes(new plDynamicEnumAttribute("PhysicsCollisionLayer")),
    PL_MEMBER_PROPERTY("ChangeNotificationMsg", m_sChangeNotificationMsg),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(GetCurrentDistance),
    PL_SCRIPT_FUNCTION_PROPERTY(GetCurrentEndPosition),
    PL_SCRIPT_FUNCTION_PROPERTY(HasHit),
  }
  PL_END_FUNCTIONS;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("Gameplay"),
    new plDirectionVisualizerAttribute(plBasisAxis::PositiveX, 0.5f, plColor::YellowGreen),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plRaycastComponent::plRaycastComponent() = default;
plRaycastComponent::~plRaycastComponent() = default;

void plRaycastComponent::Deinitialize()
{
  if (m_bForceTargetParentless)
  {
    // see end of plRaycastComponent::Update() for details
    GetWorld()->DeleteObjectDelayed(m_hRaycastEndObject);
  }

  SUPER::Deinitialize();
}

void plRaycastComponent::OnDeactivated()
{
  if (m_bDisableTargetObjectOnNoHit)
  {
    plGameObject* pEndObject = nullptr;
    if (GetWorld()->TryGetObject(m_hRaycastEndObject, pEndObject))
    {
      pEndObject->SetActiveFlag(false);
    }
  }

  SUPER::OnDeactivated();
}

void plRaycastComponent::OnSimulationStarted()
{
  m_pPhysicsWorldModule = GetWorld()->GetOrCreateModule<plPhysicsWorldModuleInterface>();

  plGameObject* pEndObject = nullptr;
  if (GetWorld()->TryGetObject(m_hRaycastEndObject, pEndObject))
  {
    if (!pEndObject->IsDynamic())
    {
      pEndObject->MakeDynamic();
    }
  }
}

void plRaycastComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  inout_stream.WriteGameObjectHandle(m_hRaycastEndObject);
  s << m_fMaxDistance;
  s << m_bDisableTargetObjectOnNoHit;
  s << m_uiCollisionLayerEndPoint;
  s << m_bForceTargetParentless;
  s << m_ShapeTypesToHit;
  s << m_sChangeNotificationMsg;
}

void plRaycastComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  const plUInt32 uiVersion = inout_stream.GetComponentTypeVersion(GetStaticRTTI());
  auto& s = inout_stream.GetStream();

  m_hRaycastEndObject = inout_stream.ReadGameObjectHandle();
  s >> m_fMaxDistance;
  s >> m_bDisableTargetObjectOnNoHit;
  s >> m_uiCollisionLayerEndPoint;

  if (uiVersion < 4)
  {
    plUInt8 uiCollisionLayerTrigger = 0;
    s >> uiCollisionLayerTrigger;

    plStringBuilder sTriggerMessage;
    s >> sTriggerMessage;
  }

  if (uiVersion >= 2)
  {
    s >> m_bForceTargetParentless;
  }

  if (uiVersion >= 3)
  {
    s >> m_ShapeTypesToHit;
  }

  if (uiVersion >= 4)
  {
    s >> m_sChangeNotificationMsg;
  }
}

plVec3 plRaycastComponent::GetCurrentEndPosition() const
{
  return GetOwner()->GetGlobalPosition() + m_fCurrentDistance * GetOwner()->GetGlobalDirForwards();
}

void plRaycastComponent::SetRaycastEndObject(const char* szReference)
{
  auto resolver = GetWorld()->GetGameObjectReferenceResolver();

  if (!resolver.IsValid())
    return;

  m_hRaycastEndObject = resolver(szReference, GetHandle(), "RaycastEndObject");
}

void plRaycastComponent::Update()
{
  if (m_hRaycastEndObject.IsInvalidated())
    return;

  if (!m_pPhysicsWorldModule)
  {
    // Happens in Prefab viewports
    return;
  }

  plGameObject* pEndObject = nullptr;
  if (!GetWorld()->TryGetObject(m_hRaycastEndObject, pEndObject))
  {
    // early out in the future
    m_hRaycastEndObject.Invalidate();
    return;
  }

  // if the owner object moved this frame, we want the latest global position as the ray starting position
  // this is especially important when the raycast component is attached to something that animates
  GetOwner()->UpdateGlobalTransform();

  bool bAnyChange = false;

  const plVec3 rayStartPosition = GetOwner()->GetGlobalPosition();
  const plVec3 rayDir = GetOwner()->GetGlobalDirForwards().GetNormalized(); // PhysX is very picky about normalized vectors

  plPhysicsCastResult hit;

  {
    plPhysicsQueryParameters queryParams(m_uiCollisionLayerEndPoint);
    queryParams.m_bIgnoreInitialOverlap = true;
    queryParams.m_ShapeTypes = m_ShapeTypesToHit;

    if (m_pPhysicsWorldModule->Raycast(hit, rayStartPosition, rayDir, m_fMaxDistance, queryParams))
    {
      if (!pEndObject->GetActiveFlag() && m_bDisableTargetObjectOnNoHit)
      {
        pEndObject->SetActiveFlag(true);
        bAnyChange = true;
      }

      if (!plMath::IsEqual(m_fCurrentDistance, hit.m_fDistance, 0.001f))
      {
        m_fCurrentDistance = hit.m_fDistance;
        bAnyChange = true;
      }
    }
    else
    {
      if (m_fCurrentDistance != m_fMaxDistance)
      {
        m_fCurrentDistance = m_fMaxDistance;
        bAnyChange = true;
      }

      if (m_bDisableTargetObjectOnNoHit)
      {
        if (pEndObject->GetActiveFlag())
        {
          pEndObject->SetActiveFlag(false);
          bAnyChange = true;
        }
      }
      else
      {
        if (!pEndObject->GetActiveFlag())
        {
          pEndObject->SetActiveFlag(true);
          bAnyChange = true;
        }
      }
    }
  }

  if (m_bForceTargetParentless && GetUserFlag(0) == false)
  {
    // only do this once
    SetUserFlag(0, true);

    // this is necessary to ensure perfect positioning when the target is originally attached to a moving object
    // that happens, for instance, when the target is part of a prefab, which includes the raycast component, of course
    // and the prefab is then attached to e.g. a character
    // without detaching the target object from all parents, it is not possible to ensure that it will never deviate from the
    // position set by the raycast component
    // since we now change ownership (target is not deleted with its former parent anymore)
    // this flag also means that the raycast component will delete the target object, when it dies
    pEndObject->SetParent(plGameObjectHandle());
  }

  const plVec3 vOldPos = pEndObject->GetGlobalPosition();
  const plVec3 vNewPos = rayStartPosition + m_fCurrentDistance * rayDir;

  if (!vOldPos.IsEqual(vNewPos, 0.001f))
  {
    pEndObject->SetGlobalPosition(vNewPos);
    bAnyChange = true;
  }

  if (!m_sChangeNotificationMsg.IsEmpty() && bAnyChange)
  {
    plMsgGenericEvent msg;
    msg.m_sMessage = m_sChangeNotificationMsg;

    GetOwner()->SendEventMessage(msg, this);
  }


  if (false)
  {
    plDebugRenderer::Line lines[] = {{rayStartPosition, vNewPos}};
    plDebugRenderer::DrawLinesOccluded(GetWorld(), lines, plColor::GreenYellow.GetDarker());
    plDebugRenderer::DrawLines(GetWorld(), lines, plColor::GreenYellow);
  }
}

PL_STATICLINK_FILE(GameComponentsPlugin, GameComponentsPlugin_Gameplay_Implementation_RaycastComponent);
