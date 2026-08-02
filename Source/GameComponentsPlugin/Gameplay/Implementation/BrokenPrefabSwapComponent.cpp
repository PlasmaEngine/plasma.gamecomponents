#include <GameComponentsPlugin/GameComponentsPCH.h>

#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <GameComponentsPlugin/Gameplay/BrokenPrefabSwapComponent.h>
#include <GameEngine/Messages/DamageMessage.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plBrokenPrefabSwapComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("BrokenPrefab", GetBrokenPrefabFile, SetBrokenPrefabFile)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Prefab")),
    PL_MEMBER_PROPERTY("Health", m_fHealth)->AddAttributes(new plDefaultValueAttribute(10.0f), new plClampValueAttribute(0.001f, plVariant())),
    PL_MEMBER_PROPERTY("ImpulseFactor", m_fImpulseFactor)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, plVariant())),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_MESSAGEHANDLERS
  {
    PL_MESSAGE_HANDLER(plMsgDamage, OnMsgDamage),
  }
  PL_END_MESSAGEHANDLERS;
  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(Break),
  }
  PL_END_FUNCTIONS;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("Gameplay"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plBrokenPrefabSwapComponent::plBrokenPrefabSwapComponent() = default;
plBrokenPrefabSwapComponent::~plBrokenPrefabSwapComponent() = default;

void plBrokenPrefabSwapComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);

  auto& s = inout_stream.GetStream();
  s << m_hBrokenPrefab;
  s << m_fHealth;
  s << m_fImpulseFactor;
  s << m_fDamageTaken;
  s << m_bBroken;
}

void plBrokenPrefabSwapComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);

  auto& s = inout_stream.GetStream();
  s >> m_hBrokenPrefab;
  s >> m_fHealth;
  s >> m_fImpulseFactor;
  s >> m_fDamageTaken;
  s >> m_bBroken;
}

void plBrokenPrefabSwapComponent::SetBrokenPrefabFile(const char* szFile)
{
  plPrefabResourceHandle hResource;

  if (!plStringUtils::IsNullOrEmpty(szFile))
  {
    hResource = plResourceManager::LoadResource<plPrefabResource>(szFile);
  }

  m_hBrokenPrefab = hResource;
}

const char* plBrokenPrefabSwapComponent::GetBrokenPrefabFile() const
{
  return m_hBrokenPrefab.IsValid() ? m_hBrokenPrefab.GetResourceID().GetData() : "";
}

void plBrokenPrefabSwapComponent::OnMsgDamage(plMsgDamage& ref_msg)
{
  if (m_bBroken)
    return;

  m_fDamageTaken += (float)ref_msg.m_fDamage;

  m_vLastHitPosition = ref_msg.m_vGlobalPosition;

  plVec3 vDir = ref_msg.m_vImpactDirection;
  if (vDir.NormalizeIfNotZero(plVec3::MakeZero()).Succeeded())
  {
    m_vLastHitImpulse = vDir * ref_msg.m_fImpulse;
  }

  if (m_fDamageTaken >= m_fHealth)
  {
    Break();
  }
}

void plBrokenPrefabSwapComponent::Break()
{
  if (m_bBroken || !IsActiveAndSimulating())
    return;

  m_bBroken = true;

  if (m_hBrokenPrefab.IsValid())
  {
    plResourceLock<plPrefabResource> pPrefab(m_hBrokenPrefab, plResourceAcquireMode::AllowLoadingFallback);

    if (pPrefab.GetAcquireResult() == plResourceAcquireResult::Final)
    {
      plHybridArray<plGameObject*, 8> createdObjects;

      plPrefabInstantiationOptions options;
      options.m_pOverrideTeamID = &GetOwner()->GetTeamID();
      options.m_pCreatedRootObjectsOut = &createdObjects;
      options.m_bForceDynamic = true;

      plTransform transform = GetOwner()->GetGlobalTransform();

      pPrefab->InstantiatePrefab(*GetWorld(), transform, options);

      // pass the last hit on to the loose pieces, so they fly instead of just collapsing in place
      if (m_fImpulseFactor > 0.0f && !m_vLastHitImpulse.IsZero(0.001f))
      {
        plMsgPhysicsAddImpulse msg;
        msg.m_vGlobalPosition = m_vLastHitPosition;
        msg.m_vImpulse = m_vLastHitImpulse * m_fImpulseFactor;

        for (plGameObject* pObject : createdObjects)
        {
          pObject->PostMessageRecursive(msg, plTime::MakeZero(), plObjectMsgQueueType::AfterInitialized);
        }
      }
    }
  }

  GetWorld()->DeleteObjectDelayed(GetOwner()->GetHandle());
}

PL_STATICLINK_FILE(GameComponentsPlugin, GameComponentsPlugin_Gameplay_Implementation_BrokenPrefabSwapComponent);
