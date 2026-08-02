#pragma once

#include <GameComponentsPlugin/GameComponentsDLL.h>

#include <Core/World/ComponentManager.h>

class plPhysicsWorldModuleInterface;

using plCreatureCrawlComponentManager = plComponentManagerSimple<class plCreatureCrawlComponent, plComponentUpdateType::WhenSimulating>;

struct plCreatureLeg
{
  plHashedString m_sLegObject;
  plUInt8 m_uiStepGroup = 0;

  plVec3 m_vRestPositionRelative;
  plGameObjectHandle m_hLegObject;
  plVec3 m_vCurTargetPosAbs;
  float m_fMoveLegFactor;
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMECOMPONENTS_DLL, plCreatureLeg);

class PL_GAMECOMPONENTS_DLL plCreatureCrawlComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plCreatureCrawlComponent, plComponent, plCreatureCrawlComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

  //////////////////////////////////////////////////////////////////////////
  // plCreatureCrawlComponent

protected:
  void Update();

  void OnSimulationStarted() override;

public:
  plCreatureCrawlComponent();
  ~plCreatureCrawlComponent();

  void SetBodyReference(const char* szReference); // [ property ]

  float m_fCastUp = 0.3f;
  float m_fCastDown = 1.0f;
  float m_fStepDistance = 0.4f;
  float m_fMinLegDistance = 0.5f;

protected:
  plGameObjectHandle m_hBody;             // [ property ]
  plHybridArray<plCreatureLeg, 4> m_Legs; // [ property ]

  const plPhysicsWorldModuleInterface* m_pPhysicsInterface = nullptr;
  plTime m_LastMove;
  plQuat m_qBodyTilt = plQuat::MakeIdentity();

private:
  const char* DummyGetter() const { return nullptr; }
};
