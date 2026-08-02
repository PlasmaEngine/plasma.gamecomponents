#pragma once

#include <Core/World/Component.h>
#include <Core/World/World.h>
#include <GameComponentsPlugin/GameComponentsDLL.h>
#include <RendererCore/Components/CameraComponent.h>

class PL_GAMECOMPONENTS_DLL plThirdPersonViewComponentManager : public plComponentManager<class plThirdPersonViewComponent, plBlockStorageType::Compact>
{
public:
  plThirdPersonViewComponentManager(plWorld* pWorld);
  ~plThirdPersonViewComponentManager();

  virtual void Initialize() override;

private:
  void Update(const plWorldModule::UpdateContext& context);

  friend class plThirdPersonViewComponent;
};

class PL_GAMECOMPONENTS_DLL plThirdPersonViewComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plThirdPersonViewComponent, plComponent, plThirdPersonViewComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

  //////////////////////////////////////////////////////////////////////////
  // plThirdPersonViewComponent

public:
  plThirdPersonViewComponent();
  ~plThirdPersonViewComponent();

  void SetTargetObject(const char* szTargetObject);
  const char* GetTargetObject() const;

  void RotateUp(plAngle angle); // [ scriptable ]

protected:
  void Update();
  void OnSimulationStarted() override;

private:
  // properties
  plString m_sTargetObject;                               // [ property ]
  plVec3 m_vTargetOffsetHigh = plVec3::MakeZero();        // [ property ]
  plVec3 m_vTargetOffsetLow = plVec3::MakeZero();         // [ property ]
  float m_fMinDistance = 0.25f;                           // [ property ]
  float m_fMaxDistance = 3.0f;                            // [ property ]
  plAngle m_MinUpRotation = plAngle::MakeFromDegree(-70); // [ property ]
  plAngle m_MaxUpRotation = plAngle::MakeFromDegree(+80); // [ property ]
  float m_fCeilingHeight = 4.0f;                          // [ property ]
  float m_fFloorHeight = 0.0f;                            // [ property ]
  plUInt8 m_uiCollisionLayer = 0;                         // [ property ]
  float m_fSweepWidth = 0.2f;                             // [ property ]
  float m_fZoomInSpeed = 10.0f;                           // [ property ]
  float m_fZoomOutSpeed = 1.0f;                           // [ property ]

  // runtime state
  plAngle m_RotateUp;
  plAngle m_CurUpRotation;
  plGameObjectHandle m_hTargetObject;
  float m_fCurDistance = 1.0f;
};