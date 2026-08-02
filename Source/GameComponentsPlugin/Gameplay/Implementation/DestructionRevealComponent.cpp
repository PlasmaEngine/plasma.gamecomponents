#include <GameComponentsPlugin/GameComponentsPCH.h>

#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <GameComponentsPlugin/Gameplay/DestructionRevealComponent.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plDestructionRevealComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("HideChildrenAtStart", m_bHideChildrenAtStart)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("RevealMargin", m_fRevealMargin)->AddAttributes(new plDefaultValueAttribute(0.5f), new plClampValueAttribute(0.0f, plVariant()), new plSuffixAttribute(" m")),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_MESSAGEHANDLERS
  {
    PL_MESSAGE_HANDLER(plMsgDestructibleChanged, OnMsgDestructibleChanged),
  }
  PL_END_MESSAGEHANDLERS;
  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(RevealAll),
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

plDestructionRevealComponent::plDestructionRevealComponent() = default;
plDestructionRevealComponent::~plDestructionRevealComponent() = default;

void plDestructionRevealComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);

  auto& s = inout_stream.GetStream();
  s << m_bHideChildrenAtStart;
  s << m_fRevealMargin;
}

void plDestructionRevealComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);

  auto& s = inout_stream.GetStream();
  s >> m_bHideChildrenAtStart;
  s >> m_fRevealMargin;
}

void plDestructionRevealComponent::OnActivated()
{
  SUPER::OnActivated();

  // receive plMsgDestructibleChanged from every destructible in the world, not just this subtree
  SetGlobalEventHandlerMode(true);
}

void plDestructionRevealComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  if (!m_bHideChildrenAtStart)
    return;

  for (auto it = GetOwner()->GetChildren(); it.IsValid(); it.Next())
  {
    it->SetActiveFlag(false);
  }
}

void plDestructionRevealComponent::OnMsgDestructibleChanged(plMsgDestructibleChanged& ref_msg)
{
  plBoundingBox box = plBoundingBox::MakeFromMinMax(ref_msg.m_vAffectedMin, ref_msg.m_vAffectedMax);

  if (!box.IsValid())
    return;

  box.Grow(plVec3(m_fRevealMargin));

  for (auto it = GetOwner()->GetChildren(); it.IsValid(); it.Next())
  {
    if (it->GetActiveFlag())
      continue;

    if (box.Contains(it->GetGlobalPosition()))
    {
      it->SetActiveFlag(true);
    }
  }
}

void plDestructionRevealComponent::RevealAll()
{
  for (auto it = GetOwner()->GetChildren(); it.IsValid(); it.Next())
  {
    it->SetActiveFlag(true);
  }
}

PL_STATICLINK_FILE(GameComponentsPlugin, GameComponentsPlugin_Gameplay_Implementation_DestructionRevealComponent);
