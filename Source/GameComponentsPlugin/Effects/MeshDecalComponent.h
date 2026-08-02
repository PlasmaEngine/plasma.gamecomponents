#pragma once

#include <Core/World/World.h>
#include <GameComponentsPlugin/GameComponentsDLL.h>
#include <RendererCore/Declarations.h>

struct plMsgExtractRenderData;

struct PL_GAMECOMPONENTS_DLL plMeshDecalDescription
{
  plUInt16 m_uiIndex = 0;
  plTexture2DResourceHandle m_hBaseColorTexture;

  plResult Serialize(plStreamWriter& inout_stream) const;
  plResult Deserialize(plStreamReader& inout_stream);
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMECOMPONENTS_DLL, plMeshDecalDescription);

////////////////////////////////////////////////////////////////////////////

using plMeshDecalComponentManager = plComponentManager<class plMeshDecalComponent, plBlockStorageType::Compact>;

class PL_GAMECOMPONENTS_DLL plMeshDecalComponent final : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plMeshDecalComponent, plComponent, plMeshDecalComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnActivated() override;
  virtual void OnDeactivated() override;

  void OnMsgExtractRenderData(plMsgExtractRenderData& msg) const;

private:
  plUInt32 Decals_GetCount() const;
  const plMeshDecalDescription& Decals_Get(plUInt32 uiIndex) const;
  void Decals_Set(plUInt32 uiIndex, const plMeshDecalDescription& desc);
  void Decals_Insert(plUInt32 uiIndex, const plMeshDecalDescription& desc);
  void Decals_Remove(plUInt32 uiIndex);

  void UpdateDecals();
  void DeleteDecals();

  plSmallArray<plMeshDecalDescription, 2> m_DecalDescs;
  plSmallArray<plDecalId, 2> m_DecalIds;
};