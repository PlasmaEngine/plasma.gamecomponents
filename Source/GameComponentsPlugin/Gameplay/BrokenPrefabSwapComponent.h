#pragma once

#include <Core/Prefabs/PrefabResource.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>
#include <GameComponentsPlugin/GameComponentsDLL.h>

struct plMsgDamage;

using plBrokenPrefabSwapComponentManager = plComponentManager<class plBrokenPrefabSwapComponent, plBlockStorageType::Compact>;

/// \brief Swaps the intact object for a pre-broken prefab once it has taken enough damage.
///
/// The standard cheap destruction path for props (chairs, monitors, crates) that do not merit a
/// full destructible-mesh bake: the object accumulates plMsgDamage; when its health is spent, the
/// broken prefab is spawned at the object's transform (inheriting the last hit's impulse) and the
/// intact object is deleted. The broken prefab typically contains loose plJoltDynamicActorComponent
/// pieces plus particles/sound.
class PL_GAMECOMPONENTS_DLL plBrokenPrefabSwapComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plBrokenPrefabSwapComponent, plComponent, plBrokenPrefabSwapComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

  //////////////////////////////////////////////////////////////////////////
  // plBrokenPrefabSwapComponent

public:
  plBrokenPrefabSwapComponent();
  ~plBrokenPrefabSwapComponent();

  void SetBrokenPrefabFile(const char* szFile); // [ property ]
  const char* GetBrokenPrefabFile() const;      // [ property ]

  /// \brief Swaps to the broken prefab immediately, regardless of remaining health.
  void Break(); // [ scriptable ]

  float m_fHealth = 10.0f;        // [ property ] Damage the object absorbs before it breaks.
  float m_fImpulseFactor = 1.0f;  // [ property ] Scales the last hit's impulse that is passed on to the broken pieces.

private:
  void OnMsgDamage(plMsgDamage& ref_msg); // [ msg handler ]

  plPrefabResourceHandle m_hBrokenPrefab;

  float m_fDamageTaken = 0.0f;
  plVec3 m_vLastHitPosition = plVec3::MakeZero();
  plVec3 m_vLastHitImpulse = plVec3::MakeZero();
  bool m_bBroken = false;
};
