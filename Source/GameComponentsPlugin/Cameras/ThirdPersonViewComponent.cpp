#include <GameComponentsPlugin/GameComponentsPCH.h>

#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Foundation/Utilities/GraphicsUtils.h>
#include <GameComponentsPlugin/Cameras/ThirdPersonViewComponent.h>

plThirdPersonViewComponentManager::plThirdPersonViewComponentManager(plWorld* pWorld)
  : plComponentManager(pWorld)
{
}

plThirdPersonViewComponentManager::~plThirdPersonViewComponentManager() = default;

void plThirdPersonViewComponentManager::Initialize()
{
  SUPER::Initialize();

  {
    auto desc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plThirdPersonViewComponentManager::Update, this);
    desc.m_Phase = UpdateFunctionDesc::Phase::PostTransform;
    desc.m_bOnlyUpdateWhenSimulating = true;

    this->RegisterUpdateFunction(desc);
  }
}

void plThirdPersonViewComponentManager::Update(const plWorldModule::UpdateContext& context)
{
  // Iterate through all components managed by this manager and update them
  for (auto it = GetComponents(); it.IsValid(); ++it)
  {
    if (it->IsActiveAndInitialized())
    {
      it->Update();
    }
  }
}


// clang-format off
PL_BEGIN_COMPONENT_TYPE(plThirdPersonViewComponent, 1, plComponentMode::Dynamic)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("TargetObject", GetTargetObject, SetTargetObject)->AddAttributes(new plDefaultValueAttribute(plStringView(".."))),
    PL_MEMBER_PROPERTY("TargetOffsetHigh", m_vTargetOffsetHigh),
    PL_MEMBER_PROPERTY("TargetOffsetLow", m_vTargetOffsetLow),
    PL_MEMBER_PROPERTY("MinDistance", m_fMinDistance)->AddAttributes(new plDefaultValueAttribute(0.25f), new plClampValueAttribute(0.05f, 10.0f)),
    PL_MEMBER_PROPERTY("MaxDistance", m_fMaxDistance)->AddAttributes(new plDefaultValueAttribute(3.0f), new plClampValueAttribute(0.5f, 100.0f)),
    PL_MEMBER_PROPERTY("MinUpRotation", m_MinUpRotation)->AddAttributes(new plDefaultValueAttribute(plAngle::MakeFromDegree(-70)), new plClampValueAttribute(plAngle::MakeFromDegree(-85), plAngle::MakeFromDegree(70))),
    PL_MEMBER_PROPERTY("MaxUpRotation", m_MaxUpRotation)->AddAttributes(new plDefaultValueAttribute(plAngle::MakeFromDegree(+80)), new plClampValueAttribute(plAngle::MakeFromDegree(-70), plAngle::MakeFromDegree(85))),
    PL_MEMBER_PROPERTY("CeilingHeight", m_fCeilingHeight)->AddAttributes(new plDefaultValueAttribute(4.0f)),
    PL_MEMBER_PROPERTY("FloorHeight", m_fFloorHeight)->AddAttributes(new plDefaultValueAttribute(0.0f)),
    PL_MEMBER_PROPERTY("CollisionLayer", m_uiCollisionLayer)->AddAttributes(new plDynamicEnumAttribute("PhysicsCollisionLayer")),
    PL_MEMBER_PROPERTY("SweepWidth", m_fSweepWidth)->AddAttributes(new plDefaultValueAttribute(0.2f), new plClampValueAttribute(0.05f, 2.0f)),
    PL_MEMBER_PROPERTY("ZoomInSpeed", m_fZoomInSpeed)->AddAttributes(new plDefaultValueAttribute(10.0f), new plClampValueAttribute(0.01f, 1000.0f)),
    PL_MEMBER_PROPERTY("ZoomOutSpeed", m_fZoomOutSpeed)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.01f, 1000.0f)),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(RotateUp, In, "angle"),
  }
  PL_END_FUNCTIONS;
}
PL_END_COMPONENT_TYPE;
// clang-format on

plThirdPersonViewComponent::plThirdPersonViewComponent() = default;
plThirdPersonViewComponent::~plThirdPersonViewComponent() = default;

void plThirdPersonViewComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);

  auto& s = inout_stream.GetStream();

  // version 1
  s << m_sTargetObject;
  s << m_vTargetOffsetHigh;
  s << m_vTargetOffsetLow;
  s << m_fMinDistance;
  s << m_fMaxDistance;
  s << m_MinUpRotation;
  s << m_MaxUpRotation;
  s << m_fCeilingHeight;
  s << m_fFloorHeight;
  s << m_uiCollisionLayer;
  s << m_fSweepWidth;
  s << m_fZoomInSpeed;
  s << m_fZoomOutSpeed;
}

void plThirdPersonViewComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  // const plUInt32 uiVersion = stream.GetComponentTypeVersion(GetStaticRTTI());

  auto& s = inout_stream.GetStream();

  // version 1
  s >> m_sTargetObject;
  s >> m_vTargetOffsetHigh;
  s >> m_vTargetOffsetLow;
  s >> m_fMinDistance;
  s >> m_fMaxDistance;
  s >> m_MinUpRotation;
  s >> m_MaxUpRotation;
  s >> m_fCeilingHeight;
  s >> m_fFloorHeight;
  s >> m_uiCollisionLayer;
  s >> m_fSweepWidth;
  s >> m_fZoomInSpeed;
  s >> m_fZoomOutSpeed;
}

void plThirdPersonViewComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  m_CurUpRotation = plMath::Lerp(m_MinUpRotation, m_MaxUpRotation, 0.5f);

  SetTargetObject(m_sTargetObject);
}

void plThirdPersonViewComponent::SetTargetObject(const char* szTargetObject)
{
  m_sTargetObject = szTargetObject;
  m_hTargetObject.Invalidate();

  if (IsActiveAndSimulating() && !m_sTargetObject.IsEmpty())
  {
    if (plGameObject* pTargetObject = GetWorld()->SearchForObject(m_sTargetObject, GetOwner()))
    {
      m_hTargetObject = pTargetObject->GetHandle();
    }
  }
}

const char* plThirdPersonViewComponent::GetTargetObject() const
{
  return m_sTargetObject;
}

void plThirdPersonViewComponent::RotateUp(plAngle angle)
{
  m_RotateUp += angle;
}

void plThirdPersonViewComponent::Update()
{
  if (m_hTargetObject.IsInvalidated())
    return;

  plGameObject* pTarget = nullptr;
  if (!GetWorld()->TryGetObject(m_hTargetObject, pTarget))
    return;

  const plTransform tTarget = pTarget->GetGlobalTransform();

  m_CurUpRotation += m_RotateUp;
  m_CurUpRotation = plMath::Clamp(m_CurUpRotation, m_MinUpRotation, m_MaxUpRotation);
  m_RotateUp = {};

  const plVec3 vOffsetCenter = plMath::Lerp(m_vTargetOffsetLow, m_vTargetOffsetHigh, 0.5f);
  plVec3 vOffset = vOffsetCenter;
  if (m_CurUpRotation.GetRadian() > 0)
  {
    const float fLerp = plMath::Unlerp(0.0f, m_MaxUpRotation.GetRadian(), m_CurUpRotation.GetRadian());
    vOffset = plMath::Lerp(vOffsetCenter, m_vTargetOffsetHigh, fLerp);
  }
  else
  {
    const float fLerp = plMath::Unlerp(0.0f, m_MinUpRotation.GetRadian(), m_CurUpRotation.GetRadian());
    vOffset = plMath::Lerp(vOffsetCenter, m_vTargetOffsetLow, fLerp);
  }

  const plVec3 vLookAtPos = tTarget.m_vPosition + tTarget.m_qRotation * vOffset;

  const plQuat qRotUp = plQuat::MakeFromAxisAndAngle(plVec3::MakeAxisY(), m_CurUpRotation);
  const plQuat qRotTotal = tTarget.m_qRotation * qRotUp;

  const plVec3 vSweepDir = qRotTotal * plVec3(-1, 0, 0);
  float fNewDistance = m_fMaxDistance;

  if (m_CurUpRotation.GetRadian() > 0)
  {
    plPlane p = plPlane::MakeFromNormalAndPoint(-plVec3::MakeAxisZ(), tTarget.m_vPosition + plVec3::MakeAxisZ() * m_fCeilingHeight);

    float fHitDist;
    if (p.GetRayIntersection(vLookAtPos, vSweepDir, &fHitDist))
    {
      fNewDistance = plMath::Min(fNewDistance, fHitDist);
    }
  }
  else
  {
    plPlane p = plPlane::MakeFromNormalAndPoint(plVec3::MakeAxisZ(), tTarget.m_vPosition + plVec3::MakeAxisZ() * m_fFloorHeight);

    float fHitDist;
    if (p.GetRayIntersection(vLookAtPos, vSweepDir, &fHitDist))
    {
      fNewDistance = plMath::Min(fNewDistance, fHitDist);
    }
  }

  if (plPhysicsWorldModuleInterface* pPhysics = GetWorld()->GetOrCreateModule<plPhysicsWorldModuleInterface>())
  {
    const plTime tDiff = GetWorld()->GetClock().GetTimeDiff();

    plPhysicsCastResult res1;
    plPhysicsQueryParameters params;
    params.m_ShapeTypes = plPhysicsShapeType::Static;
    params.m_uiCollisionLayer = m_uiCollisionLayer;
    if (pPhysics->SweepTestSphere(res1, m_fSweepWidth, vLookAtPos, vSweepDir, fNewDistance, params))
    {
      fNewDistance = plMath::Max(m_fMinDistance, res1.m_fDistance);
    }

    if (m_fCurDistance > fNewDistance)
    {
      m_fCurDistance = plMath::Lerp(m_fCurDistance, fNewDistance, plMath::Min(1.0f, tDiff.AsFloatInSeconds() * m_fZoomInSpeed));
    }
    else
    {
      m_fCurDistance = plMath::Lerp(m_fCurDistance, fNewDistance, plMath::Min(1.0f, tDiff.AsFloatInSeconds() * m_fZoomOutSpeed));
    }
  }

  const plVec3 vLookFromPos = vLookAtPos + m_fCurDistance * vSweepDir;
  GetOwner()->SetGlobalTransformToLookAt(vLookFromPos, vLookAtPos);
}