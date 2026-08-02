#include <GameEngine/GameEnginePCH.h>

#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <GameComponentsPlugin/Animation/CreatureCrawlComponent.h>
#include <RendererCore/AnimationSystem/AnimPoseGenerator.h>
#include <RendererCore/AnimationSystem/Skeleton.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_TYPE(plCreatureLeg, plNoBase, 1, plRTTIDefaultAllocator<plCreatureLeg>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("LegName", m_sLegObject),
    PL_MEMBER_PROPERTY("StepGroup", m_uiStepGroup),
  }
  PL_END_PROPERTIES;
}
PL_END_STATIC_REFLECTED_TYPE;

PL_BEGIN_COMPONENT_TYPE(plCreatureCrawlComponent, 1, plComponentMode::Dynamic);
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Body", DummyGetter, SetBodyReference)->AddAttributes(new plGameObjectReferenceAttribute()),
    PL_MEMBER_PROPERTY("CastUp", m_fCastUp)->AddAttributes(new plDefaultValueAttribute(0.3f)),
    PL_MEMBER_PROPERTY("CastDown", m_fCastDown)->AddAttributes(new plDefaultValueAttribute(1.0f)),
    PL_MEMBER_PROPERTY("StepDistance", m_fStepDistance)->AddAttributes(new plDefaultValueAttribute(0.4f)),
    PL_MEMBER_PROPERTY("MinLegDistance", m_fMinLegDistance)->AddAttributes(new plDefaultValueAttribute(0.5f), new plClampValueAttribute(0.1f, 1.0f)),
    PL_ARRAY_MEMBER_PROPERTY("Legs", m_Legs),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
      new plCategoryAttribute("Animation"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

plCreatureCrawlComponent::plCreatureCrawlComponent() = default;
plCreatureCrawlComponent::~plCreatureCrawlComponent() = default;

void plCreatureCrawlComponent::SetBodyReference(const char* szReference)
{
  auto resolver = GetWorld()->GetGameObjectReferenceResolver();

  if (!resolver.IsValid())
    return;

  m_hBody = resolver(szReference, GetHandle(), "Body");
}

void plCreatureCrawlComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_fCastUp;
  s << m_fCastDown;
  s << m_fStepDistance;
  s << m_fMinLegDistance;

  inout_stream.WriteGameObjectHandle(m_hBody);
  s << m_Legs.GetCount();
  for (plUInt32 i = 0; i < m_Legs.GetCount(); ++i)
  {
    s << m_Legs[i].m_sLegObject;
    s << m_Legs[i].m_uiStepGroup;
  }
}

void plCreatureCrawlComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  // const plUInt32 uiVersion = inout_stream.GetComponentTypeVersion(GetStaticRTTI());
  auto& s = inout_stream.GetStream();

  s >> m_fCastUp;
  s >> m_fCastDown;
  s >> m_fStepDistance;
  s >> m_fMinLegDistance;

  m_hBody = inout_stream.ReadGameObjectHandle();
  plUInt32 numLegs = 0;
  s >> numLegs;
  m_Legs.SetCount(numLegs);
  for (plUInt32 i = 0; i < m_Legs.GetCount(); ++i)
  {
    s >> m_Legs[i].m_sLegObject;
    s >> m_Legs[i].m_uiStepGroup;
  }
}

void plCreatureCrawlComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  const plTransform invTrans = GetOwner()->GetGlobalTransform().GetInverse();

  for (auto& leg : m_Legs)
  {
    leg.m_vCurTargetPosAbs.SetZero();
    leg.m_fMoveLegFactor = 0.99f;
    leg.m_vRestPositionRelative.SetZero();
    leg.m_hLegObject.Invalidate();

    if (plGameObject* pLeg = GetOwner()->FindChildByName(leg.m_sLegObject))
    {
      leg.m_hLegObject = pLeg->GetHandle();
      leg.m_vRestPositionRelative = invTrans * pLeg->GetGlobalPosition();
    }
  }
}

void plCreatureCrawlComponent::Update()
{
  if (m_pPhysicsInterface == nullptr)
  {
    m_pPhysicsInterface = GetWorld()->GetModule<plPhysicsWorldModuleInterface>();
    return;
  }

  const plUInt32 uiNumLegs = m_Legs.GetCount();

  const plVec3 vCenterPos = GetOwner()->GetGlobalPosition();
  const plQuat qCenterRot = GetOwner()->GetGlobalRotation();

  plHybridArray<plVec3, 8> vNewTargetPos;
  vNewTargetPos.SetCount(uiNumLegs);

  plWorld* pWorld = GetWorld();

  // TODO: make step duration configurable
  // TODO: make step height configurable
  const plTime tStepDuration = plTime::MakeFromMilliseconds(150);
  const float fStepHeight = 0.3f;
  const float fMoveAdd = plMath::Min<float>(1.0f, GetWorld()->GetClock().GetTimeDiff().AsFloatInSeconds() / tStepDuration.AsFloatInSeconds());

  plHybridArray<bool, 8> bLegMoving;
  bLegMoving.SetCount(uiNumLegs);
  bool bAnyLegMoving = false;

  for (plUInt32 i = 0; i < uiNumLegs; ++i)
  {
    vNewTargetPos[i] = qCenterRot * (m_Legs[i].m_vRestPositionRelative + plVec3(0, 0, m_fCastUp));
    bLegMoving[i] = m_Legs[i].m_fMoveLegFactor < 1.0f;
    bAnyLegMoving = bAnyLegMoving || bLegMoving[i];
  }

  bool bComputeBodyTilt = true;

  for (plUInt32 i = 0; i < uiNumLegs; ++i)
  {
    if (m_Legs[i].m_hLegObject.IsInvalidated())
      continue;

    // find a place where the leg can stand
    // if none found at the rest position, try pulling the leg closer to the body in two steps (unless forbidden, see m_fMinLegDistance)

    plPhysicsCastResult res;
    if (m_pPhysicsInterface->Raycast(res, vCenterPos + vNewTargetPos[i], plVec3(0, 0, -1.0f), m_fCastDown, plPhysicsQueryParameters(0, plPhysicsShapeType::Static)))
    {
      vNewTargetPos[i] = res.m_vPosition;
    }
    else if (m_fMinLegDistance < 1.0f && m_pPhysicsInterface->Raycast(res, vCenterPos + vNewTargetPos[i] * plMath::Lerp(m_fMinLegDistance, 1.0f, 0.5f), plVec3(0, 0, -1.0f), m_fCastDown, plPhysicsQueryParameters(0, plPhysicsShapeType::Static)))
    {
      vNewTargetPos[i] = res.m_vPosition;
    }
    else if (m_fMinLegDistance < 1.0f && m_pPhysicsInterface->Raycast(res, vCenterPos + vNewTargetPos[i] * m_fMinLegDistance, plVec3(0, 0, -1.0f), m_fCastDown, plPhysicsQueryParameters(0, plPhysicsShapeType::Static)))
    {
      vNewTargetPos[i] = res.m_vPosition;
    }
    else
    {
      bComputeBodyTilt = false;

      vNewTargetPos[i] += vCenterPos;
      m_Legs[i].m_vCurTargetPosAbs = plMath::Lerp(m_Legs[i].m_vCurTargetPosAbs, vNewTargetPos[i], fMoveAdd);
    }
  }

  plGameObject* pBody;
  if (!m_hBody.IsInvalidated() && pWorld->TryGetObject(m_hBody, pBody) && m_Legs.GetCount() == 4 /* TODO: remove safety check*/)
  {
    const plQuat qOwnerRot = GetOwner()->GetGlobalRotation();

    plQuat qNewBodyTilt;

    if (bComputeBodyTilt)
    {
      // TODO: compute body tilt from N points:
      // const plTransform invTrans = GetOwner()->GetGlobalTransform().GetInverse();

      // plVec3 vAvgDir(0);

      // for (plUInt32 i = 0; i < uiNumLegs; ++i)
      //{
      //   plVec3 vDir = invTrans * vNewTargetPos[i];
      //   vAvgDir += vDir;
      // }

      plVec3 vAvgLegLeft = qOwnerRot.GetInverse() * ((vNewTargetPos[0] + vNewTargetPos[2]) * 0.5f);
      plVec3 vAvgLegRight = qOwnerRot.GetInverse() * ((vNewTargetPos[1] + vNewTargetPos[3]) * 0.5f);
      vAvgLegLeft.x = 0.0f;
      vAvgLegRight.x = 0.0f;

      plVec3 vAvgLegFwd = qOwnerRot.GetInverse() * ((vNewTargetPos[2] + vNewTargetPos[3]) * 0.5f);
      plVec3 vAvgLegBack = qOwnerRot.GetInverse() * ((vNewTargetPos[0] + vNewTargetPos[1]) * 0.5f);
      vAvgLegFwd.y = 0.0f;
      vAvgLegBack.y = 0.0f;

      const plVec3 vSideTilt = (vAvgLegRight - vAvgLegLeft).GetNormalized();
      const plQuat qSideTilt = plQuat::MakeShortestRotation(plVec3(0, 1, 0), vSideTilt);

      const plVec3 vFwdTilt = (vAvgLegFwd - vAvgLegBack).GetNormalized();
      const plQuat qFwdTilt = plQuat::MakeShortestRotation(plVec3(1, 0, 0), vFwdTilt);

      qNewBodyTilt = qSideTilt * qFwdTilt;
    }
    else
    {
      qNewBodyTilt = plQuat::MakeIdentity();
    }

    m_qBodyTilt = plQuat::MakeSlerp(m_qBodyTilt, qNewBodyTilt, fMoveAdd);
    pBody->SetGlobalRotation(qOwnerRot * m_qBodyTilt);

    pBody->UpdateGlobalTransform();
  }

  if (!bAnyLegMoving)
  {
    float fMaxDistSqr = 0;
    plUInt32 uiStepGroup = plInvalidIndex;

    // check whether to move a leg
    for (plUInt32 i = 0; i < uiNumLegs; ++i)
    {
      const float ds = (m_Legs[i].m_vCurTargetPosAbs - vNewTargetPos[i]).GetLengthSquared();

      if (ds > fMaxDistSqr)
      {
        fMaxDistSqr = ds;
        uiStepGroup = m_Legs[i].m_uiStepGroup;
      }
    }

    const plTime tNow = pWorld->GetClock().GetAccumulatedTime();

    // TODO: make step delay configurable ?

    if (fMaxDistSqr >= plMath::Square(m_fStepDistance) ||
        (fMaxDistSqr >= plMath::Square(m_fStepDistance * 0.25) && (tNow - m_LastMove > plTime::MakeFromSeconds(0.6f))))
    {
      m_LastMove = tNow;

      for (plUInt32 i = 0; i < uiNumLegs; ++i)
      {
        if (m_Legs[i].m_uiStepGroup == uiStepGroup)
        {
          // move all legs from the same group simultaneously
          m_Legs[i].m_fMoveLegFactor = 0.0f;
        }
      }
    }
  }

  for (plUInt32 i = 0; i < uiNumLegs; ++i)
  {
    if (bLegMoving[i])
    {
      // TODO: nicer step curve (sine instead of linear)

      m_Legs[i].m_fMoveLegFactor += fMoveAdd;

      if (m_Legs[i].m_fMoveLegFactor >= 1.0f)
      {
        m_Legs[i].m_fMoveLegFactor = 1.0f;
        m_Legs[i].m_vCurTargetPosAbs = vNewTargetPos[i];
      }
      else
      {
        float fSrcHeight = m_Legs[i].m_vCurTargetPosAbs.z;
        float fDstHeight = vNewTargetPos[i].z;
        float fMidHeight = plMath::Max(fSrcHeight, fDstHeight) + fStepHeight;

        if (m_Legs[i].m_fMoveLegFactor < 0.5f)
        {
          vNewTargetPos[i].z = plMath::Lerp(m_Legs[i].m_vCurTargetPosAbs.z, fMidHeight, m_Legs[i].m_fMoveLegFactor * 2.0f);
        }
        else
        {
          vNewTargetPos[i].z = plMath::Lerp(fMidHeight, vNewTargetPos[i].z, (m_Legs[i].m_fMoveLegFactor - 0.5f) * 2.0f);
        }
      }

      vNewTargetPos[i] = plMath::Lerp(m_Legs[i].m_vCurTargetPosAbs, vNewTargetPos[i], m_Legs[i].m_fMoveLegFactor);
    }
    else
    {
      vNewTargetPos[i] = m_Legs[i].m_vCurTargetPosAbs;
    }

    plGameObject* pLegTarget;
    if (pWorld->TryGetObject(m_Legs[i].m_hLegObject, pLegTarget))
    {
      pLegTarget->SetGlobalPosition(vNewTargetPos[i]);
    }
  }
}


PL_STATICLINK_FILE(GameComponentsPlugin, GameComponentsPlugin_Animation_Implementation_CreatureCrawlComponent);
