#pragma once

#include <Core/World/EventMessageHandlerComponent.h>
#include <Core/World/World.h>
#include <GameComponentsPlugin/GameComponentsDLL.h>

struct plMsgDestructibleChanged;

using plDestructionRevealComponentManager = plComponentManager<class plDestructionRevealComponent, plBlockStorageType::Compact>;

/// \brief Activates its (initially hidden) child objects when a destructible breaks open nearby.
///
/// Place this on a parent object whose children are the things that should only appear once the
/// structure around them is destroyed: cables and ducts in a ceiling plenum, insulation and rebar
/// inside walls, pipes behind panels. The children are deactivated when the simulation starts and
/// re-activated as soon as a plJoltDestructibleComponent loses chunks in their vicinity - cables
/// drop into view exactly where the ceiling opened up.
///
/// Works with any child object; plFakeRopeComponent cables are the typical use (they start
/// simulating and sag naturally on activation).
class PL_GAMECOMPONENTS_DLL plDestructionRevealComponent : public plEventMessageHandlerComponent
{
  PL_DECLARE_COMPONENT_TYPE(plDestructionRevealComponent, plEventMessageHandlerComponent, plDestructionRevealComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnActivated() override;
  virtual void OnSimulationStarted() override;

  //////////////////////////////////////////////////////////////////////////
  // plDestructionRevealComponent

public:
  plDestructionRevealComponent();
  ~plDestructionRevealComponent();

  /// \brief Activates all child objects inside the box, regardless of pending destruction events.
  void RevealAll(); // [ scriptable ]

  bool m_bHideChildrenAtStart = true; // [ property ] Deactivate all direct children when the simulation starts.
  float m_fRevealMargin = 0.5f;       // [ property ] How far around the destroyed region children are still revealed (meters).

private:
  void OnMsgDestructibleChanged(plMsgDestructibleChanged& ref_msg); // [ msg handler ]
};
