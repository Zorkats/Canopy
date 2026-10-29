#pragma once
#include <cstddef>

// Generated from the command classification; do not edit by hand.
//
// kAllowed: the game commands a safe Lua mod sees, each one only while a
// save is loaded and the player is on a map. kGuarded: the commands the
// TLS guard wraps, which refuse a safe mod and are logged for an unsafe
// one. The two lists never share a name.
//
// Treated as persistent_write although classified otherwise:
//   Collision2DBodySetUserTag: the call walk reaches status_idvalue_set.
//   CreatureGetNextPos: the call walk reaches status_value_set.
//   CreatureGetRandomItemID: the call walk reaches status_idvalue_set.
//   CreatureSetBasePos: creature home positions may be saved.
//   EventPauseLateNightEvent: the call walk reaches event_reward.
//   EventRestartLateNightEvent: the call walk reaches event_reward.
//   GameClearStory: a story command, even if the retail body looks empty.
//   GameSetFirstSubquestList: the call walk reaches flag_notify.
//   GameTimeAdd: kept off the allowlist with the other game time commands.
//   GimmickCreateDummyFromDataID: the call walk reaches mapobj_pos.
//   GimmickCreateInventoryUI: the call walk reaches status_idvalue_set.
//   GimmickCreateProcessingManpuUI: the call walk reaches status_idvalue_set.
//   GimmickCreateProcessingUI: the call walk reaches status_idvalue_set.
//   GimmickCreateProcessingUI_Far: the call walk reaches status_idvalue_set.
//   GimmickDeleteProcessingUI: the call walk reaches status_idvalue_set.
//   GimmickSetUseItemID: gimmick item ids may be saved.
//   ItemSetBrandName: item brand names are saved.
//   LivestockRemoveStatus: removes saved livestock status.
//   LivestockSetBrandName: livestock names are saved.
//   LivestockSetDogName: the dog's name is saved.
//   LivestockSetFeature: livestock features are saved.
//   LivestockSetGrowStatus: livestock growth is saved.
//   LivestockSetMoodRate: livestock mood is saved.
//   LivestockSetNotEatLandID: livestock feeding land is saved.
//   LivestockSetStyle: livestock style is saved.
//   LivestockStatusSetBasePos: livestock home position is saved.
//   LivestockStatusSetFoodValue: livestock food value is saved.
//   MapSetScarecrowFlag: scarecrow flags are map state the save keeps.
//   MapUpdateScarecrowFlag: scarecrow flags are map state the save keeps.
//   NpcCheckSecretQuestVillager: reaches a flag set and a letter add at depth 3.
//   PlayerSaveStyleBuffer: writes the saved player style.
//   PlayerSetConsumeEnergy: reaches a status value set at depth 3.
//   TaskCreate_GamePopupSelect: reaches a flag set.
//   TaskCreate_HelpPopupSelect: reaches a flag set.
//   TaskCreate_MainGame: starts the main game task, which reaches a flag set.
//   TaskCreate_TimeSkip: skips game time.
//
// Left out of the sandbox, though they write nothing themselves:
//   Collision2DClearContactBeginEvent: can clear vanilla event triggers.
//   Collision2DClearContactEndEvent: can clear vanilla event triggers.
//   Collision2DClearContactEvent: can clear vanilla event triggers.
//   CreatureScriptResume: resumes a vanilla creature script.
//   CreatureSetHP: creature status may be saved.
//   DebugSetConfig: changes debug configuration.
//   EventCheckStart: can start a vanilla event.
//   EventOpenRestaurantOrderPopup: opens a screen that gives rewards.
//   EventOpenSeasonMinigameUI: opens a minigame that gives rewards.
//   EventUpdate: advances vanilla events.
//   EventUpdateTimeCheckEvent: can start timed vanilla events.
//   GameCreateMailboxPopup: opens the mailbox, which marks letters read.
//   GameHideQuest: may change quest state.
//   GimmickCreateBundlePopupUI: opens a screen that gives rewards.
//   GimmickScriptActivate: runs a vanilla gimmick script.
//   GimmickScriptResume: resumes a vanilla gimmick script.
//   MapObjectRemoveAreaHold: changes vanilla map object areas.
//   MapObjectSetBodyHitEnabled: switches map collision off.
//   MapObjectSetIsEnabled: can disable map objects and triggers.
//   MapRemoveDiffMap: map diffs follow story progress.
//   MapSetIsHitEnabled: switches map collision off.
//   MapWarpByCollision: moves the player to another map.
//   MapWarpPos: moves the player to another map.
//   MapWarpPosDirect: moves the player to another map.
//   NpcSetBasePos: NPC status may be saved.
//   NpcSetStyle: NPC status may be saved.
//   NpcSetupSchedule: NPC schedules drive events.
//   NpcTalkStart: starts a vanilla dialogue.
//   TalkChangeTalk: switches a running dialogue.
//   TalkCreateData: builds dialogue that can run talk commands.
//   TalkCreateTask: starts a dialogue task.
//   ThreadLockBegin: can block vanilla scripts.
//   ThreadLockEnd: can release a vanilla script lock.
//   UnitWarpByAreaPos: can drop the player into an event trigger.
//   UnitWarpByCollision: can drop the player into an event trigger.
//   UnitWarpByGimmickParts: can drop the player into an event trigger.
//   UnitWarpByMapEvent: moves the player onto a map event.
//   UnitWarpDirect: can drop the player into an event trigger.
namespace lua_commands {

constexpr const char* kAllowed[] = {
    "AnimeAddLayerCallbackText",
    "AnimeAddSeqChangeCallback",
    "AnimeChange",
    "AnimeCreate",
    "AnimeCreateCopy",
    "AnimeCreate_MapDraw",
    "AnimeCreate_MapDrawAnimeObject",
    "AnimeEndWait",
    "AnimeFrameWait",
    "AnimeGetAABB",
    "AnimeGetDataID",
    "AnimeGetEndFrame",
    "AnimeGetFrame",
    "AnimeGetFrameF",
    "AnimeGetLayerFrame",
    "AnimeGetLocalScale",
    "AnimeGetPgParam",
    "AnimeGetPgParamTable",
    "AnimeGetPlayInfo",
    "AnimeGetReplaceTextureIDByName",
    "AnimeGetResourceEndFrame",
    "AnimeGetResourceLayer",
    "AnimeGetSeq",
    "AnimeGetSpeed",
    "AnimeIsEnd",
    "AnimeIsExistSeq",
    "AnimeKeyCallbackWait",
    "AnimeReserveInstancingSeq",
    "AnimeSetAddColor",
    "AnimeSetBillboardRenderStep",
    "AnimeSetEndDelete",
    "AnimeSetFrame",
    "AnimeSetFrameF",
    "AnimeSetGroupID",
    "AnimeSetIsParticleTracking",
    "AnimeSetIsUseInstancing",
    "AnimeSetLayerFrame",
    "AnimeSetLayerVisible",
    "AnimeSetLocalScale",
    "AnimeSetPgParam",
    "AnimeSetPgParamArray",
    "AnimeSetPlayInfo",
    "AnimeSetReplaceShader",
    "AnimeSetReplaceTexture",
    "AnimeSetSeq",
    "AnimeSetSeqAction",
    "AnimeSetSeqAction_Collision",
    "AnimeSetSpeed",
    "AnimeSetTextForLayer",
    "AnimeSetVisible",
    "AnimeSkip",
    "AttackContainsFeature",
    "AttackGetID",
    "BaseEndWait",
    "BaseIsExist",
    "BaseListEndWait",
    "BgmFadeWait",
    "BgmGetBgmID",
    "BgmGetPlayFrame",
    "BgmGetVolume",
    "BgmPause",
    "BgmPlay",
    "BgmPlaywithCrossFade",
    "BgmPlaywithFade",
    "BgmResume",
    "BgmSetPlayFrame",
    "BgmSetVolume",
    "BgmStop",
    "CameraAddLocalPos",
    "CameraAddLocalRot",
    "CameraAddLocalViewAngle",
    "CameraAddLocalZoom",
    "CameraClearVib",
    "CameraGetFar",
    "CameraGetNear",
    "CameraGetViewAngle",
    "CameraGetViewport",
    "CameraGetZoom",
    "CameraPopParam",
    "CameraPushParam",
    "CameraResetLocalParam",
    "CameraSetFar",
    "CameraSetNear",
    "CameraSetTrace",
    "CameraSetVib",
    "CameraSetViewAngle",
    "CameraSetZoom",
    "CaptureBackScreenShot",
    "ClassGetClassID",
    "Collision2DAddBody",
    "Collision2DAddBodyByAnime",
    "Collision2DAddBodyByAnimeResource",
    "Collision2DAddFixMoveVec",
    "Collision2DAddForceVec",
    "Collision2DAddMoveVec",
    "Collision2DAddShapeByAnimeResource",
    "Collision2DAddShape_Circle",
    "Collision2DAddShape_Line",
    "Collision2DAddShape_Poly3",
    "Collision2DAddShape_Poly4",
    "Collision2DAddShape_Rect",
    "Collision2DBodyCallChanged",
    "Collision2DBodyGetUserData",
    "Collision2DBodyGetUserTag",
    "Collision2DBodyIsTargetHit",
    "Collision2DBodySetIsHitEnabled",
    "Collision2DClearShape",
    "Collision2DCreate",
    "Collision2DCreateFromMapAnchor",
    "Collision2DGetBody",
    "Collision2DGetBodyCount",
    "Collision2DGetBodyUniqueID",
    "Collision2DGetCenter",
    "Collision2DGetCollisionCellPosList",
    "Collision2DGetContactList",
    "Collision2DGetContactNormal",
    "Collision2DGetContactUserDataList",
    "Collision2DGetDestMask",
    "Collision2DGetIsHitEnabled",
    "Collision2DGetLastPushVec",
    "Collision2DGetLastVelocity",
    "Collision2DGetPredictionMovePos",
    "Collision2DGetRadius",
    "Collision2DGetSrcMask",
    "Collision2DGetUserData",
    "Collision2DIsBodyPassable",
    "Collision2DIsBodyPassableByBody",
    "Collision2DIsHit",
    "Collision2DIsSensorBody",
    "Collision2DIsTargetHit",
    "Collision2DRaycastAll_Circle",
    "Collision2DRaycastBody",
    "Collision2DRaycast_Circle",
    "Collision2DSetDestMask",
    "Collision2DSetIsHitEnabled",
    "Collision2DSetMoveVec",
    "Collision2DSetPushFlag",
    "Collision2DSetSrcMask",
    "Collision2DUpdateFromAnime",
    "Collision2DUpdateFromAnimeAlways",
    "Collision2DUpdateFromAnime_UsePlayerFrame",
    "ColorMake",
    "CreatureAddBullet",
    "CreatureClearBullet",
    "CreatureCreateDemonLifeGaugeUI",
    "CreatureCreateLightSensor",
    "CreatureCreateSight",
    "CreatureDeleteLightSensor",
    "CreatureDeleteSight",
    "CreatureEndLightSensing",
    "CreatureEndSightSearch",
    "CreatureEndSightTrack",
    "CreatureGetBasePos",
    "CreatureGetDataID",
    "CreatureGetGimmickID",
    "CreatureGetHP",
    "CreatureGetHPMax",
    "CreatureGetLikeability",
    "CreatureGetModuleName",
    "CreatureGetPartsTable",
    "CreatureGetPlacementAnchorName",
    "CreatureGetReplaceTextureID",
    "CreatureGetStatusByFeatureID",
    "CreatureIsActive",
    "CreatureIsFeature",
    "CreatureIsInSight",
    "CreatureIsInSightAny",
    "CreatureIsJumpOverFence",
    "CreatureIsLightHitting",
    "CreatureRegisterHeartbeat",
    "CreatureSearchByAnchorName",
    "CreatureSearchPlaneGhostList",
    "CreatureSetAttack",
    "CreatureSetIsVisibleDemonLifeGaugeUI",
    "CreatureSetSightBodyFilter",
    "CreatureShowRankUpPopup",
    "CreatureStartLightSensing",
    "CreatureStartSightSearch",
    "CreatureStartSightTrack",
    "CreatureStatusSearchByDataID",
    "CreatureStopScript",
    "CreatureUnregisterHeartbeat",
    "CropsEatingAnimeEndWait",
    "CropsGenerateEatingTicket",
    "CropsGetDataIDByLandID",
    "CropsGetForm",
    "CropsGetGrowth",
    "CropsGetObject",
    "CropsGetSubDataIDByLandID",
    "CropsIsEating",
    "CropsIsEatingTarget",
    "CropsIsExistenceLandID",
    "CropsIsStatusLandID",
    "CropsSetEatingAnime",
    "CropsSetLRReverse",
    "CropsShake",
    "DataGetString",
    "DatabaseGetData",
    "DatabaseGetIDByEnumName",
    "DatabaseGetIDTable",
    "DatabaseIsExistData",
    "DebugDisp",
    "DebugGetCPURate",
    "DebugGetConfig",
    "DebugGetGPURate",
    "DebugGetRecodingSetting",
    "DebugIsOn",
    "DebugOutputPerformanceLog",
    "DebugPause",
    "DebugPrint",
    "DebugPrintGameTime",
    "DebugScreenText",
    "DebugSetFastForwardSpeed",
    "DebugSetIsOutputLog",
    "DegreeDistance",
    "DropItemGetPopVec",
    "DummyCreateFromCreatureDataID",
    "DummyCreateFromLivestockDataID",
    "DummyCreateFromNpcDataID",
    "DummyCreateFromStatus",
    "DungeonGetFloor",
    "DungeonGetIsNow",
    "DungeonGetMapID",
    "DungeonGetNextMapID",
    "DungeonGetPlayerPopPos",
    "Easing",
    "EasingGetRate",
    "EasingGetRate_Interger",
    "EventCheckConditions",
    "EventCheckFairLivestockExist",
    "EventCheckLateNightEventClear",
    "EventGetInariBundleItem",
    "EventGetNextEventTime",
    "EventGetShootGameReward",
    "EventGetStatusByDataID",
    "EventGetStatusIDValue",
    "EventGetStatusIDValueList",
    "EventGetStatusTableValue",
    "EventGetStatusValue",
    "FadeAccelMake",
    "FadeEasingAccelMake",
    "FadeEasingMake",
    "FadeMake",
    "FrameBufferAssociatePredraw",
    "FrameBufferCapture",
    "FrameBufferCaptureScreen",
    "FrameBufferCreate",
    "FrameBufferCreate_Table",
    "GameAddPushLog",
    "GameCheckFlags",
    "GameCheckRequiredMoney",
    "GameClearQuestPopup",
    "GameCloseBundleUI",
    "GameCloseConstructionUI",
    "GameCloseInventoryItemDetail",
    "GameCloseUpperQuestPopup",
    "GameCloseUpperSubQuestPopup",
    "GameCreateFractionPopupUI",
    "GameFlagOffWait",
    "GameFlagOnWait",
    "GameForceCloseLog",
    "GameGenerateGameTimeStopTicket",
    "GameGetAngleCodeToAngle",
    "GameGetAngleToAngleCode",
    "GameGetBirdDropItemID",
    "GameGetExchangeItemCount",
    "GameGetExchangeItemID",
    "GameGetExistUnreadLetter",
    "GameGetFlag",
    "GameGetFurniturePurchased",
    "GameGetIDValue",
    "GameGetInventoryAddableCount",
    "GameGetInventoryItemByActionID",
    "GameGetInventoryItemByFeature",
    "GameGetInventoryItemByIndex",
    "GameGetInventoryItemByItemID",
    "GameGetInventoryItemCount",
    "GameGetInventoryItemIndex",
    "GameGetInventorySelectedIndex",
    "GameGetInventorySelectedItem",
    "GameGetInventorySelectedTool",
    "GameGetInventorySpaceCount",
    "GameGetItemPrice",
    "GameGetMainGameMapInfo",
    "GameGetMainGamePad",
    "GameGetMoney",
    "GameGetNextDayWeatherID",
    "GameGetRandomLateNightItemID",
    "GameGetSaveDataSlotIndex",
    "GameGetSelectToolIndex",
    "GameGetShippingBoxItemCount",
    "GameGetTableValue",
    "GameGetToolItemByFeature",
    "GameGetValue",
    "GameGetValueMax",
    "GameGetValueMin",
    "GameGetWeatherID",
    "GameHaveItemByFeature",
    "GameIsCheckProbability",
    "GameIsItemFeature",
    "GameIsRegisteredEncyclopedia",
    "GameIsWeatherFeature",
    "GameOpenUpperQuestPopup",
    "GameOpenUpperSubQuestPopup",
    "GameReqReplayWeather",
    "GameSetHUDShowOption",
    "GameSetVibrationInventorySlot",
    "GameShowFacilityReleasedPopup",
    "GameShowIntangeblePopup",
    "GameShowLowerQuestPopup",
    "GameShowTips",
    "GameTimeConvertGameTime",
    "GameTimeConvertRawSecond",
    "GameTimeGetNextDaybreak",
    "GameTimeGetRateInDay",
    "GameTimeGetRawSecond",
    "GameTimeGetRawSecondWithoutDay",
    "GameTimeGetSeason",
    "GameTimeGetTime",
    "GameTimeGetTimeZone",
    "GameTimeGetTimeZoneStart",
    "GameTimeGetTodayTime",
    "GameTimeGetWeekday",
    "GameTimeIsAfterDay",
    "GameTimeIsOverToday",
    "GameTimeIsReach",
    "GameTimeIsTimeIn",
    "GameTimeResumeWait",
    "GameTimeWaitUntil",
    "GimmickCreateAttackAnimeObject",
    "GimmickCreateDummyFromStatus",
    "GimmickDeleteInventoryUI",
    "GimmickDeleteProcessingManpuUI",
    "GimmickDeleteProcessingUI_Far",
    "GimmickGenerateAccessingTicket",
    "GimmickGetAnimeID",
    "GimmickGetBaseSeqID",
    "GimmickGetColorReplaceTexID",
    "GimmickGetCraftRecipe",
    "GimmickGetDataID",
    "GimmickGetGachaponItemCount",
    "GimmickGetGachaponItemID",
    "GimmickGetGateStraight",
    "GimmickGetGateTileTable",
    "GimmickGetHarvestItemID",
    "GimmickGetInProcess",
    "GimmickGetInventoryItem",
    "GimmickGetIsProcessUIDetailView",
    "GimmickGetModuleName",
    "GimmickGetMyhouseStatus",
    "GimmickGetName",
    "GimmickGetOutSEID",
    "GimmickGetPartsIndex",
    "GimmickGetPartsLayerPos",
    "GimmickGetPartsTable",
    "GimmickGetRandomGachaponID",
    "GimmickGetReplaceTextureID",
    "GimmickGetRetrievePopVec",
    "GimmickGetScriptArgValue",
    "GimmickGetStatusByLandID",
    "GimmickGetStatusListByLandID",
    "GimmickGetTag",
    "GimmickGetTileTable",
    "GimmickGetTime",
    "GimmickGetUniqueName",
    "GimmickGetUseItemID",
    "GimmickIsAccessing",
    "GimmickIsEndTime",
    "GimmickIsFeature",
    "GimmickIsHarvestItem",
    "GimmickOpenProcessingUI",
    "GimmickScriptPause",
    "GimmickSetIsometricEnable",
    "GimmickSetParent",
    "GimmickSetProcessIcon",
    "GimmickSetProcessUI",
    "GimmickSetReplaceTexture",
    "GimmickStatusNearSearchByDataID",
    "GimmickStatusSearch",
    "GimmickStatusSearchByDataID",
    "IDtoText",
    "IsCallTalk",
    "IsometricCreateFrontLineByAnime",
    "IsometricSetEnable",
    "IsometricUpdateFrontLineByAnime",
    "ItemExistHuntReward",
    "ItemGetActionID",
    "ItemGetActionValue",
    "ItemGetCategory",
    "ItemGetCategoryByID",
    "ItemGetEnergy",
    "ItemGetFeatureItemID",
    "ItemGetGimmickScriptArgName",
    "ItemGetID",
    "ItemGetIconAnime",
    "ItemGetIconID",
    "ItemGetLevel",
    "ItemGetMaxEnergy",
    "ItemGetRank",
    "ItemIsBranded",
    "ItemIsFeature",
    "ItemIsNewItem",
    "ItemSetHUDHelpMessage",
    "ItemShowIntangeblePopup",
    "ItemShowRankUpPopup",
    "ItemShowSacredPopup",
    "LineIntersection",
    "LiveStockSearchFeedCrops",
    "LivestockGetAnimeID",
    "LivestockGetBrandedCount",
    "LivestockGetDogName",
    "LivestockGetGrowStatus",
    "LivestockGetHomeGimmickStatus",
    "LivestockGetID",
    "LivestockGetLoveLevel",
    "LivestockGetLoveRate",
    "LivestockGetMoodRate",
    "LivestockGetMoodRateInfluencedGhost",
    "LivestockGetPlacementID",
    "LivestockGetRankupLaterTable",
    "LivestockGetStatusByFeature",
    "LivestockGetStatusByPlacement",
    "LivestockIsActive",
    "LivestockIsBranded",
    "LivestockIsFeature",
    "LivestockIsGimmickInversion",
    "LivestockIsInsideHouse",
    "LivestockIsLayer",
    "LivestockIsOpenHouseWindow",
    "LivestockShowBrandedPopup",
    "LivestockShowRankUpPopup",
    "LivestockStatusGetBasePos",
    "LivestockStatusGetFoodValue",
    "LivestockStatusGetHomeAreaID",
    "LivestockStatusGetHomeOutsidePos",
    "LivestockStatusGetHomePos",
    "LivestockStatusGetName",
    "MapAddCameraFocusTarget",
    "MapAddUpdateAllObject",
    "MapAddUpdateRect",
    "MapAddUpdateRectObject",
    "MapAdjustCamera",
    "MapClearCameraFocusTarget",
    "MapConvAreaToWorld",
    "MapConvWorldToArea",
    "MapConvertScreenToMapPos",
    "MapDrawResourceAddAABB",
    "MapDrawResourceAddAnime",
    "MapDrawResourceAddIcon",
    "MapDrawResourceAddShadow",
    "MapDrawResourceClearAnime",
    "MapDrawResourceClearIcon",
    "MapDrawResourceSetShadowColor",
    "MapDrawResourceSetShadowVisible",
    "MapDrawResourceUpdateAABB",
    "MapEnvironmentSePlay",
    "MapEnvironmentSeStop",
    "MapEventGetCheckPointList",
    "MapEventIsFeature",
    "MapEventSearch",
    "MapExcludeEntity",
    "MapFilterIsAccel",
    "MapFilterResetAccel",
    "MapFilterSetAccel",
    "MapFilterSetHorrorLevel",
    "MapGetAdjustGridPos",
    "MapGetAnchorPos",
    "MapGetAreaID",
    "MapGetAreaRect",
    "MapGetAreaStatusIDValue",
    "MapGetAreaStatusIDValueList",
    "MapGetAreaStatusTableValue",
    "MapGetAreaStatusValue",
    "MapGetCameraZoomMode",
    "MapGetCollision2DWorld",
    "MapGetConnectGimmickStatusList",
    "MapGetCurrentMainAreaID",
    "MapGetEnvironmentSeRate",
    "MapGetFarmlandID",
    "MapGetFarmlandWarter",
    "MapGetGroundElementRandomPos",
    "MapGetHouseAreaID",
    "MapGetIsHitEnabled",
    "MapGetLandID",
    "MapGetLandPos",
    "MapGetMyHouseAreaID",
    "MapGetNearLandIDList",
    "MapGetObject",
    "MapGetPlayerPopPos",
    "MapGetRanchArea",
    "MapGetRectLandIDList",
    "MapGetReserveRect",
    "MapGetWarpPos",
    "MapIsAreaExist",
    "MapIsCollisionArea",
    "MapIsExistMapObject",
    "MapIsFeature",
    "MapIsGroundDataElemnt",
    "MapIsIndoor",
    "MapLoadArea",
    "MapLoadEndWait",
    "MapLoadMapObject",
    "MapLoadResourceByRect",
    "MapObjectAddHoldAreaObject",
    "MapObjectAddHoldAreaPos",
    "MapObjectAddHoldAreaRect",
    "MapObjectGenerateDisableReleaseTicket",
    "MapObjectGet8AngleCode",
    "MapObjectGetAABB",
    "MapObjectGetAngle",
    "MapObjectGetAreaID",
    "MapObjectGetBody",
    "MapObjectGetIsDisableRelease",
    "MapObjectGetIsEnabled",
    "MapObjectGetMapGroupID",
    "MapObjectGetObject",
    "MapObjectGetPos",
    "MapObjectGetScale",
    "MapObjectGetStatus",
    "MapObjectGetStatusClassID",
    "MapObjectGetStatusIDValue",
    "MapObjectGetStatusIDValueList",
    "MapObjectGetStatusTableValue",
    "MapObjectGetStatusValue",
    "MapObjectGetStatus_Creature",
    "MapObjectGetStatus_Crops",
    "MapObjectGetStatus_Gimmick",
    "MapObjectGetStatus_Item",
    "MapObjectGetStatus_Livestock",
    "MapObjectGetStatus_Npc",
    "MapObjectGetStatus_Unit",
    "MapObjectGetUniqueID",
    "MapObjectIsExist",
    "MapObjectSearch",
    "MapObjectSearch_Creature",
    "MapObjectSearch_Gimmick",
    "MapObjectSearch_Item",
    "MapObjectSearch_Livestock",
    "MapObjectSearch_Npc",
    "MapObjectSearch_Unit",
    "MapObjectSetAABB",
    "MapObjectSetAngle",
    "MapObjectSetIsDisableRelease",
    "MapObjectSetScale",
    "MapObjectStatusValueChangeWait",
    "MapObjectStatusValueChangeWaitTable",
    "MapObjectSyncBodyPos",
    "MapPosIsInScreen",
    "MapRefresh",
    "MapReleaseArea",
    "MapReleaseMapObject",
    "MapReloadEnvironment",
    "MapRemoveCameraFocusTarget",
    "MapResumeExcludeEntity",
    "MapSearchAnchorLandIDPrefix",
    "MapSearchAnchorPosPrefix",
    "MapSearchStatusInRect",
    "MapSearchStatusInRect_Creature",
    "MapSearchStatusInRect_Gimmick",
    "MapSearchStatusInRect_Item",
    "MapSearchStatusInRect_Livestock",
    "MapSearchStatusInRect_Npc",
    "MapSetAreaTransparentBG",
    "MapSetCameraZoomMode",
    "MapSetDreamFilter",
    "MapSetHideFilter",
    "MapSetIsCameraControlEnable",
    "MapSetIsCameraControlFocusEnable",
    "MapSetIsCameraControlZoomEnable",
    "MapSetPassFrameBufferMesh",
    "MapStashMapObject",
    "MapStashMapObjectClassAll",
    "MapStashMapObjectOnStashGimmmick",
    "MapStopEnvrionment",
    "MapUnstashMapObjectAll",
    "MapUpdateNavigationMesh",
    "MapUpdateScreen",
    "Max",
    "MeshCreateMapGroundCell",
    "MeshCreateRect",
    "MeshCreateTextureRect",
    "Min",
    "MsgButtonWait",
    "MsgGetVolume",
    "MsgIsPlaying",
    "MsgPlay",
    "MsgReady",
    "MsgSetVolume",
    "MsgStop",
    "MsgWait",
    "NpcGetBasePos",
    "NpcGetDataID",
    "NpcGetExistMapGameValue",
    "NpcGetInitialPos",
    "NpcGetLoveRate",
    "NpcGetPresentCommandID",
    "NpcGetPresentDataID",
    "NpcGetPresentEnable",
    "NpcGetStyle",
    "NpcGetSubQuestAchievable",
    "NpcGetTalkDataID",
    "NpcSearchStatus",
    "NpcSetupAnimeStyle",
    "NpcShowLikabilityRankupPopup",
    "ObjectActionWait",
    "ObjectAddAction",
    "ObjectAddAction_KeyFramePos",
    "ObjectAddChild",
    "ObjectAddRotate",
    "ObjectAddTraceAndChild",
    "ObjectAddTraceAndChild_KeepTransform",
    "ObjectClearAction",
    "ObjectClearChild",
    "ObjectClearState",
    "ObjectCreate",
    "ObjectDeleteAllDummy",
    "ObjectDeleteTrace",
    "ObjectDeleteTraceAll",
    "ObjectDistanceXYWait",
    "ObjectGetCenter",
    "ObjectGetChild",
    "ObjectGetChildList",
    "ObjectGetChildRecursive",
    "ObjectGetColor",
    "ObjectGetCom",
    "ObjectGetLayer",
    "ObjectGetName",
    "ObjectGetParent",
    "ObjectGetPos",
    "ObjectGetPosXY",
    "ObjectGetRotate",
    "ObjectGetScale",
    "ObjectGetTraceChild",
    "ObjectGetTraceOffset",
    "ObjectGetTransform",
    "ObjectGetVisible",
    "ObjectIsAction",
    "ObjectIsExistStateByClassName",
    "ObjectIsHaveCom",
    "ObjectIsLayerExist",
    "ObjectIsPossesionActionByClassName",
    "ObjectIsPossessionAction",
    "ObjectLayerExistOnWait",
    "ObjectSearch",
    "ObjectSetCenter",
    "ObjectSetColor",
    "ObjectSetIsTraceLostDelete",
    "ObjectSetPos",
    "ObjectSetPosXY",
    "ObjectSetPosZ",
    "ObjectSetRotate",
    "ObjectSetScale",
    "ObjectSetTrace",
    "ObjectSetTraceFlag",
    "ObjectSetTraceIsometric",
    "ObjectSetTraceLayerID",
    "ObjectSetTraceOffset",
    "ObjectSetTransform",
    "ObjectSetVisible",
    "PadCommandInputWait",
    "PadCommandInputWaitorTime",
    "PadCommandLockBlackList",
    "PadCommandLockWhiteList",
    "PadCommandOnWait",
    "PadCreate",
    "PadGetCommandFrame",
    "PadGetCommandPressFrame",
    "PadGetCommandVector",
    "PadGetIsPlayerInputOff",
    "PadIsActive",
    "PadIsCommand",
    "PadIsCommandOn",
    "PadIsLockOne",
    "PadKeyOff",
    "PadKeyOn",
    "PadPopDisable",
    "PadPushDisable",
    "PadSetCommand",
    "PadSetDisableFrame",
    "PadSetIsEnabled",
    "PadSetIsPlayerInputOff",
    "PadSetIsReject",
    "PadSetVibTime",
    "PlayerClearBullet",
    "PlayerGenerateHeartBeatLevelTicket",
    "PlayerGet8AngleCode",
    "PlayerGetEnterDirection",
    "PlayerGetFrontLandID",
    "PlayerGetHP",
    "PlayerGetHeartBeatLevel",
    "PlayerGetLightType",
    "PlayerGetMouse8AngleCode",
    "PlayerGetObject",
    "PlayerGetPadMove8AngleCode",
    "PlayerGetST",
    "PlayerGetSoulObject",
    "PlayerGetStatus",
    "PlayerGetStick8AngleCode",
    "PlayerIsInteractionCommand",
    "PlayerIsInteractionCommandAccepting",
    "PlayerIsInteractionCommandAcceptingByCollision",
    "PlayerIsInteractionCommandByCollision",
    "PlayerIsInteractionCommandByCollisionWithName",
    "PlayerIsInvincible",
    "PlayerIsLightOn",
    "PlayerLightOff",
    "PlayerLightOn",
    "PlayerSetAnimeCallbackDelete",
    "PlayerSetHeartBeatLevel",
    "PlayerSetIsPadEnable",
    "PlayerSetItemJoyMotion",
    "PlayerSetLightLayer",
    "PlayerSetPumpAction",
    "PlayerSetScriptAction",
    "PlayerSetupAnimeColor",
    "PlayerSetupAnimeStyle",
    "PlayerSwapStyleBuffer",
    "PostEffectCreate",
    "PostEffectPopBackup",
    "PostEffectPushBackup",
    "PrimitiveCreateLayerLine",
    "PrimitiveCreateRect",
    "PrimitiveCreateTextureRect",
    "Random",
    "RandomRange",
    "RandomSelectIndex",
    "RectAddPos",
    "RectGetExpand",
    "RectIsCrossRect",
    "RectIsInPosXY",
    "RectMake",
    "RectMake_Box",
    "RectMake_RangeXY",
    "RectMake_RangeXZ",
    "RectMake_WH",
    "RenderSetFixShadowRequest",
    "RenderStepGetCamera",
    "RenderStepGetShadowBias",
    "RenderStepGetShadowFar",
    "RenderStepGetShadowNear",
    "RenderStepGetShadowViewAngle",
    "RenderStepGetShadowZoom",
    "RenderStepPopParam",
    "RenderStepPushParam",
    "RenderStepSetDisabledTechnique",
    "RenderStepSetIsRender",
    "RenderStepSetShadowAlpha",
    "RenderStepSetShadowAreaCenter",
    "RenderStepSetShadowBias",
    "RenderStepSetShadowEdgeAlpha",
    "RenderStepSetShadowEdgeMinAlpha",
    "RenderStepSetShadowFar",
    "RenderStepSetShadowNear",
    "RenderStepSetShadowViewAngle",
    "RenderStepSetShadowZoom",
    "ResourceHolderCreate",
    "ResourceLoadAnime",
    "ResourceLoadTexture",
    "ResourceLoadWait",
    "ScreenAspectPanelSetFadeIn",
    "ScreenAspectPanelSetFadeOut",
    "ScreenCreateAspectPanel",
    "ScreenFade",
    "ScreenFadeWait",
    "ScreenFlash",
    "ScriptGetPlayerLocalValue",
    "ScriptGetRegisterUIUnionObjectByID",
    "ScriptGetRegisterUIUnionObjectByName",
    "ScriptGetShareValue",
    "ScriptIsExistPlayerLocalValue",
    "ScriptRemovePlayerLocalValue",
    "ScriptSetPlayerLocalValue",
    "ScriptSystemIsRegionAsia",
    "ScriptUnregisterUIUnionObject",
    "Se3DCreate",
    "Se3DCreateListener",
    "Se3DPause",
    "Se3DResume",
    "Se3DSetComMasterEnable",
    "Se3DSetVolume",
    "Se3DSetupAnimeKeyFrame",
    "Se3DStopWithFade",
    "SeClearIDReplaceAll",
    "SeExist3DListener",
    "SeGetVolume",
    "SeIsPlaying",
    "SeIsPlayingByDataID",
    "SePlay",
    "SePlay3D",
    "SePlay3DXY",
    "SePlay3DXYwithPan",
    "SeSetIDReplace",
    "SeSetVolume",
    "SeStop",
    "SeStopAll",
    "SeWait",
    "SystemGetLanguageID",
    "SystemIsPS4",
    "SystemIsPS5",
    "SystemIsSteam",
    "SystemIsSwitch",
    "SystemIsSwitch2",
    "SystemIsWin",
    "SystemSetCPUBoostMode",
    "SystemTerminate",
    "TalkAddEmptyLog",
    "TalkAddLogMsgID",
    "TalkAddTalkLog",
    "TalkChangeBU",
    "TalkCreateBU",
    "TalkEndWait",
    "TalkGetBUObject",
    "TalkRegisterTalker",
    "TalkReleaseBU",
    "TalkTaskGetTalkData",
    "TaskFindChildTaskByClassID",
    "TaskGetLivelihoodMainTaskID",
    "TaskGetManager",
    "TaskGetPad",
    "TaskGetRootObject",
    "TaskIsExistByClassName",
    "TaskSoundPauseForMenu",
    "TaskWaitByClassName",
    "TextCreate",
    "TextID",
    "TextureGetHeight",
    "TextureGetWidth",
    "ThreadAllEndWait",
    "ThreadEndWait",
    "ThreadIsExist",
    "ThreadIsLocked",
    "TimeConvertTickToFrame",
    "TimeGetElapsedFrame",
    "TimeGetFrame",
    "TimeGetMagnification",
    "TimeGetTime",
    "TimeSetFastforwardFrame",
    "TimeSetFrameSkipOff",
    "TimeSetPauseFrame",
    "TransformMake",
    "UIClose",
    "UICreateFromFairy",
    "UICreateLetterBox",
    "UIGetLineH",
    "UIListGetListCount",
    "UIListGetListIndex",
    "UIListGetSelectObject",
    "UIOpen",
    "UIWait",
    "UnitAddKeepMapObject",
    "UnitAddMoveVec",
    "UnitAnimeEndWait",
    "UnitAnimeFrameWait",
    "UnitAnimeIsEnd",
    "UnitCallKeepObjectForceRelese",
    "UnitCheckObject",
    "UnitClearKeepMapObject",
    "UnitCreateAttackAnimeObject",
    "UnitCreateAttackAnimeObjectWithCallback",
    "UnitCreateBulletAttackAnimeObject",
    "UnitGet8AngleCode",
    "UnitGetAngle",
    "UnitGetEscapeVec",
    "UnitGetEscapeVecWithChangeDirection",
    "UnitGetForwardVec",
    "UnitGetItsumadeFirePillarPos",
    "UnitGetMovableRangePos",
    "UnitGetMoveSpeed",
    "UnitGetRandomFanRangePos",
    "UnitGetRandomMovableRangePos",
    "UnitGetRandomMovableRangePosWait",
    "UnitGetSeq",
    "UnitGetSeqType",
    "UnitIsLookAtTargetPos",
    "UnitIsLookAtTargetPosWithinAngle",
    "UnitPopActionState",
    "UnitRemoveKeepMapObject",
    "UnitRouteWalkWait",
    "UnitRouteWalkWaitToArea",
    "UnitRouteWalkWaitToBody",
    "UnitSet8AngleCode",
    "UnitSetActionState",
    "UnitSetAngle",
    "UnitSetAnimeFrame",
    "UnitSetItemJoyMotion",
    "UnitSetPartsSeqType",
    "UnitSetRandomAngle",
    "UnitSetSeq",
    "UnitSetSeqType",
    "UnitStatusGetRouteNextPos",
    "UnitStatusIsRouteEnd",
    "UnitStatusIsRouteFailed",
    "UnitStatusSearchRoute",
    "UnitWalk",
    "UnitWalkTargetFrame",
    "UnitWalkTargetSpeed",
    "UnitWalkTargetSpeedWait",
    "UnitWalkWait",
    "Vector2Add",
    "Vector2Dot",
    "Vector2GetDistance",
    "Vector2GetLength",
    "Vector2GetNormalize",
    "Vector2GetRotate",
    "Vector2IsZero",
    "Vector2Make",
    "Vector2MakeFromVector3",
    "Vector2Mul",
    "Vector2Sub",
    "Vector3Add",
    "Vector3Get8AngleCode",
    "Vector3GetAngleXY",
    "Vector3GetAngleXZ",
    "Vector3GetCenterRotate",
    "Vector3GetDistance",
    "Vector3GetDistanceXY",
    "Vector3GetLength",
    "Vector3GetLookAt",
    "Vector3GetNormalize",
    "Vector3GetRandom",
    "Vector3GetRotate",
    "Vector3GetTargetVelocityXY",
    "Vector3IsZero",
    "Vector3Make",
    "Vector3MakeFrom8AngleCode",
    "Vector3MakeFromScalar",
    "Vector3MakeFromVector2",
    "Vector3Mul",
    "Vector3Sub",
    "VectorGet8AngleNomalize",
    "Wait",
    "WaitJustFrame",
};

constexpr const char* kGuarded[] = {
    "Collision2DAddContactBeginEvent",
    "Collision2DAddContactEndEvent",
    "Collision2DAddContactEvent",
    "Collision2DBodySetUserTag",
    "CreatureAddLikeability",
    "CreatureAddLikeabilityCatAll",
    "CreatureCreate",
    "CreatureCreateSetAnchorPos",
    "CreatureDropItem",
    "CreatureGetNextPos",
    "CreatureGetRandomItemID",
    "CreatureRemoveAll",
    "CreatureRemoveAllbyDataID",
    "CreatureRemoveStatus",
    "CreatureSetBasePos",
    "CreatureSetLikeability",
    "CreatureSetRandomAnchorPos",
    "CreatureVanish",
    "CreatureVanishAllByDataID",
    "CropsCreate",
    "CropsCreateAllAnchorPos",
    "CropsCreateAllAtOnce",
    "CropsDestoryRect",
    "CropsDestroy",
    "CropsHarvestNaturalObject",
    "CropsRemoveAllAnchorPos",
    "CropsRemoveRange",
    "CropsSetGrowth",
    "CropsSetRemoveEvent",
    "CropsSetWater",
    "CropsSubDestroy",
    "DropItemCreate",
    "DropItemCreateByStatus",
    "DropItemRemoveByItemID",
    "DropItemRemoveByUniqueID",
    "DropItemSetIsDelete",
    "DropItemStopAbsorption",
    "DropItemThrowItem",
    "DungeonClear",
    "DungeonPlacementEnemyAndVein",
    "DungeonRegisterGimmick",
    "DungeonSetAllDeliteObject",
    "DungeonSetFloor",
    "DungeonSetIsNow",
    "DungeonSetPopGimmickFlag",
    "DungeonSetupNextFloor",
    "DungeonWarpNextFloor",
    "DungeonWarpSetFloor",
    "EventAddDailyUpdate",
    "EventAddGameFlagChange",
    "EventAddGameIDValueChange",
    "EventAddGameValueChange",
    "EventAddHorrorPlacement",
    "EventAddOrderCount",
    "EventAddStatusIDValueChangeEvent",
    "EventAddStatusTableValueChangeEvent",
    "EventAddStatusValueChangeEvent",
    "EventAddTimeEvent",
    "EventAddTimeEvent_StandAlone",
    "EventCreatePlacementPlaneGhostTableList",
    "EventEndInariBundle",
    "EventEndLateNightEvent",
    "EventGainReward",
    "EventGetTrophy",
    "EventMealAtRestaurant",
    "EventPauseLateNightEvent",
    "EventPlacementLostBooks",
    "EventRemove",
    "EventRestartLateNightEvent",
    "EventSetFairFlagByReply",
    "EventSetPlacementDarumaAndStone",
    "EventSetPlacementHatGhost",
    "EventSetPlacementMizuchi",
    "EventSetPlacementPlaneGhost",
    "EventSetRemoveDarumaAndStone",
    "EventSetReplacementCreature",
    "EventSetReplacementItem",
    "EventSetReplacementObject",
    "EventSetStatusIDValue",
    "EventSetStatusTableValue",
    "EventSetStatusValue",
    "EventStartLateNightEvent",
    "EventSubmitLostBooks",
    "EventUpdateStoreSales",
    "FarmlandAddWater",
    "GameAddDay",
    "GameAddInventoryItem",
    "GameAddLetter",
    "GameAddSecretQuestCount",
    "GameAddToolItem",
    "GameAddValue",
    "GameClearInventoryItem",
    "GameClearInventoryItemByItemID",
    "GameClearStory",
    "GameCloseHuntingLodgeUI",
    "GameCloseTips",
    "GameDailyResultOmmition",
    "GameDailyUpdate",
    "GameDropTreasureBox",
    "GameLoadContinueData",
    "GameLoadTodayData",
    "GameMainGameReload",
    "GameRegisterEncyclopedia",
    "GameSetCotninueMode",
    "GameSetFirstSubquestList",
    "GameSetFlag",
    "GameSetIDValue",
    "GameSetInventorySelectedIndex",
    "GameSetMoney",
    "GameSetNextDayWeather",
    "GameSetSelectInventoryItem",
    "GameSetSelectToolIndex",
    "GameSetTableValue",
    "GameSetValue",
    "GameSetWeatherID",
    "GameSubInventoryItem",
    "GameSubInventoryItemByID",
    "GameSwapInventoryItem",
    "GameTimeAdd",
    "GameTimeSetTagDay",
    "GameTimeSetTime",
    "GimmickCheckCapture",
    "GimmickCheckFishing",
    "GimmickCheckMyHouseBed",
    "GimmickClearInventoryItem",
    "GimmickCreate",
    "GimmickCreateAllAtOnce",
    "GimmickCreateDummyFromDataID",
    "GimmickCreateInventoryUI",
    "GimmickCreateProcessingManpuUI",
    "GimmickCreateProcessingUI",
    "GimmickCreateProcessingUI_Far",
    "GimmickDeleteProcessingUI",
    "GimmickDropInventoryItem",
    "GimmickExtractorPopItem",
    "GimmickGetGraveItem",
    "GimmickLivingTankIncreaseFish",
    "GimmickPopDeadEnd",
    "GimmickProcessDropItem",
    "GimmickProcessPopItem",
    "GimmickProcessPushItem",
    "GimmickRemove",
    "GimmickRemoveAllByDataID",
    "GimmickRetrieveItem",
    "GimmickSetDropItem",
    "GimmickSetProcessTime",
    "GimmickSetPushItem",
    "GimmickSetPushItemStatus",
    "GimmickSetStartTime",
    "GimmickSetSubInventoryItem",
    "GimmickSetUseItemID",
    "GimmickUpdateFence",
    "GimmickUpdateGate",
    "GimmickVanish",
    "ItemConstructionBox",
    "ItemGetRecipeItem",
    "ItemPushCertificate",
    "ItemSetBrandName",
    "ItemSetEnergy",
    "ItemSetIsBroken",
    "ItemSetIsEnabled",
    "LivestockAddLivestockProductsCount",
    "LivestockAddLoveRate",
    "LivestockCreate",
    "LivestockCreateDropItem",
    "LivestockEatHouseFood",
    "LivestockOutsightMove",
    "LivestockRemoveStatus",
    "LivestockSetBrandName",
    "LivestockSetDogName",
    "LivestockSetFeature",
    "LivestockSetGrowStatus",
    "LivestockSetLoveRate",
    "LivestockSetMoodRate",
    "LivestockSetNotEatLandID",
    "LivestockSetStyle",
    "LivestockStatusSetBasePos",
    "LivestockStatusSetFoodValue",
    "LivestockWarpHomePos",
    "MapAddAreaStatusIDValueChangeEvent",
    "MapAddAreaStatusValueChangeEvent",
    "MapAddAreaStatusValueChangeEvent_StandAlone",
    "MapCreateFarmland",
    "MapCreaturePop",
    "MapForcedRemoveEntity",
    "MapObjectAddEventCallback",
    "MapObjectAddStatusIDValueChangeEvent",
    "MapObjectAddStatusTableValueChangeEvent",
    "MapObjectAddStatusValueChangeEvent",
    "MapObjectRemoveStatusIDValue",
    "MapObjectRemoveStatusTableValue",
    "MapObjectRemoveStatusValue",
    "MapObjectSetPos",
    "MapObjectSetStatusIDValue",
    "MapObjectSetStatusTableValue",
    "MapObjectSetStatusValue",
    "MapSetAreaStatusIDValue",
    "MapSetAreaStatusTableValue",
    "MapSetAreaStatusValue",
    "MapSetBushAreaFilter",
    "MapSetLostHouseFilter",
    "MapSetPassFilter",
    "MapSetScarecrowFlag",
    "MapStatusCreate",
    "MapStatusRemove",
    "MapUpdateScarecrowFlag",
    "MappAddAreaStatusIDValueChangeEvent",
    "NpcAchieveSubQuest",
    "NpcAddLoveRate",
    "NpcCheckSecretQuestVillager",
    "NpcCheckTalkReward",
    "NpcGetSubQuestRequest",
    "NpcSendPresent",
    "PlayerAddHPMax",
    "PlayerAddSTGaugeMax",
    "PlayerDamage",
    "PlayerHeal",
    "PlayerLiftingEnd",
    "PlayerSaveStyleBuffer",
    "PlayerSetConsumeEnergy",
    "PlayerSetHP",
    "PlayerSetName",
    "PlayerSetST",
    "ScriptCreate",
    "TaskCreate_AutoSave",
    "TaskCreate_GamePopupSelect",
    "TaskCreate_HelpPopupSelect",
    "TaskCreate_MainGame",
    "TaskCreate_SaveList",
    "TaskCreate_TimeSkip",
    "ThreadCreate",
};

// For each kGuarded entry: true when it starts game scripts (a script,
// a thread or a deferred callback), false when it writes the save.
constexpr bool kGuardedStarts[] = {
    true,  // Collision2DAddContactBeginEvent
    true,  // Collision2DAddContactEndEvent
    true,  // Collision2DAddContactEvent
    false,  // Collision2DBodySetUserTag
    false,  // CreatureAddLikeability
    false,  // CreatureAddLikeabilityCatAll
    false,  // CreatureCreate
    false,  // CreatureCreateSetAnchorPos
    false,  // CreatureDropItem
    false,  // CreatureGetNextPos
    false,  // CreatureGetRandomItemID
    false,  // CreatureRemoveAll
    false,  // CreatureRemoveAllbyDataID
    false,  // CreatureRemoveStatus
    false,  // CreatureSetBasePos
    false,  // CreatureSetLikeability
    false,  // CreatureSetRandomAnchorPos
    false,  // CreatureVanish
    false,  // CreatureVanishAllByDataID
    false,  // CropsCreate
    false,  // CropsCreateAllAnchorPos
    false,  // CropsCreateAllAtOnce
    false,  // CropsDestoryRect
    false,  // CropsDestroy
    false,  // CropsHarvestNaturalObject
    false,  // CropsRemoveAllAnchorPos
    false,  // CropsRemoveRange
    false,  // CropsSetGrowth
    true,  // CropsSetRemoveEvent
    false,  // CropsSetWater
    false,  // CropsSubDestroy
    false,  // DropItemCreate
    false,  // DropItemCreateByStatus
    false,  // DropItemRemoveByItemID
    false,  // DropItemRemoveByUniqueID
    false,  // DropItemSetIsDelete
    false,  // DropItemStopAbsorption
    false,  // DropItemThrowItem
    false,  // DungeonClear
    false,  // DungeonPlacementEnemyAndVein
    false,  // DungeonRegisterGimmick
    false,  // DungeonSetAllDeliteObject
    false,  // DungeonSetFloor
    false,  // DungeonSetIsNow
    false,  // DungeonSetPopGimmickFlag
    false,  // DungeonSetupNextFloor
    false,  // DungeonWarpNextFloor
    false,  // DungeonWarpSetFloor
    true,  // EventAddDailyUpdate
    true,  // EventAddGameFlagChange
    true,  // EventAddGameIDValueChange
    true,  // EventAddGameValueChange
    false,  // EventAddHorrorPlacement
    false,  // EventAddOrderCount
    true,  // EventAddStatusIDValueChangeEvent
    true,  // EventAddStatusTableValueChangeEvent
    true,  // EventAddStatusValueChangeEvent
    true,  // EventAddTimeEvent
    true,  // EventAddTimeEvent_StandAlone
    false,  // EventCreatePlacementPlaneGhostTableList
    false,  // EventEndInariBundle
    false,  // EventEndLateNightEvent
    false,  // EventGainReward
    false,  // EventGetTrophy
    false,  // EventMealAtRestaurant
    false,  // EventPauseLateNightEvent
    false,  // EventPlacementLostBooks
    false,  // EventRemove
    false,  // EventRestartLateNightEvent
    false,  // EventSetFairFlagByReply
    false,  // EventSetPlacementDarumaAndStone
    false,  // EventSetPlacementHatGhost
    false,  // EventSetPlacementMizuchi
    false,  // EventSetPlacementPlaneGhost
    false,  // EventSetRemoveDarumaAndStone
    false,  // EventSetReplacementCreature
    false,  // EventSetReplacementItem
    false,  // EventSetReplacementObject
    false,  // EventSetStatusIDValue
    false,  // EventSetStatusTableValue
    false,  // EventSetStatusValue
    false,  // EventStartLateNightEvent
    false,  // EventSubmitLostBooks
    false,  // EventUpdateStoreSales
    false,  // FarmlandAddWater
    false,  // GameAddDay
    false,  // GameAddInventoryItem
    false,  // GameAddLetter
    false,  // GameAddSecretQuestCount
    false,  // GameAddToolItem
    false,  // GameAddValue
    false,  // GameClearInventoryItem
    false,  // GameClearInventoryItemByItemID
    false,  // GameClearStory
    false,  // GameCloseHuntingLodgeUI
    false,  // GameCloseTips
    false,  // GameDailyResultOmmition
    false,  // GameDailyUpdate
    false,  // GameDropTreasureBox
    false,  // GameLoadContinueData
    false,  // GameLoadTodayData
    false,  // GameMainGameReload
    false,  // GameRegisterEncyclopedia
    false,  // GameSetCotninueMode
    false,  // GameSetFirstSubquestList
    false,  // GameSetFlag
    false,  // GameSetIDValue
    false,  // GameSetInventorySelectedIndex
    false,  // GameSetMoney
    false,  // GameSetNextDayWeather
    false,  // GameSetSelectInventoryItem
    false,  // GameSetSelectToolIndex
    false,  // GameSetTableValue
    false,  // GameSetValue
    false,  // GameSetWeatherID
    false,  // GameSubInventoryItem
    false,  // GameSubInventoryItemByID
    false,  // GameSwapInventoryItem
    false,  // GameTimeAdd
    false,  // GameTimeSetTagDay
    false,  // GameTimeSetTime
    false,  // GimmickCheckCapture
    false,  // GimmickCheckFishing
    false,  // GimmickCheckMyHouseBed
    false,  // GimmickClearInventoryItem
    false,  // GimmickCreate
    false,  // GimmickCreateAllAtOnce
    false,  // GimmickCreateDummyFromDataID
    false,  // GimmickCreateInventoryUI
    false,  // GimmickCreateProcessingManpuUI
    false,  // GimmickCreateProcessingUI
    false,  // GimmickCreateProcessingUI_Far
    false,  // GimmickDeleteProcessingUI
    false,  // GimmickDropInventoryItem
    false,  // GimmickExtractorPopItem
    false,  // GimmickGetGraveItem
    false,  // GimmickLivingTankIncreaseFish
    false,  // GimmickPopDeadEnd
    false,  // GimmickProcessDropItem
    false,  // GimmickProcessPopItem
    false,  // GimmickProcessPushItem
    false,  // GimmickRemove
    false,  // GimmickRemoveAllByDataID
    false,  // GimmickRetrieveItem
    false,  // GimmickSetDropItem
    false,  // GimmickSetProcessTime
    false,  // GimmickSetPushItem
    false,  // GimmickSetPushItemStatus
    false,  // GimmickSetStartTime
    false,  // GimmickSetSubInventoryItem
    false,  // GimmickSetUseItemID
    false,  // GimmickUpdateFence
    false,  // GimmickUpdateGate
    false,  // GimmickVanish
    false,  // ItemConstructionBox
    false,  // ItemGetRecipeItem
    false,  // ItemPushCertificate
    false,  // ItemSetBrandName
    false,  // ItemSetEnergy
    false,  // ItemSetIsBroken
    false,  // ItemSetIsEnabled
    false,  // LivestockAddLivestockProductsCount
    false,  // LivestockAddLoveRate
    false,  // LivestockCreate
    false,  // LivestockCreateDropItem
    false,  // LivestockEatHouseFood
    false,  // LivestockOutsightMove
    false,  // LivestockRemoveStatus
    false,  // LivestockSetBrandName
    false,  // LivestockSetDogName
    false,  // LivestockSetFeature
    false,  // LivestockSetGrowStatus
    false,  // LivestockSetLoveRate
    false,  // LivestockSetMoodRate
    false,  // LivestockSetNotEatLandID
    false,  // LivestockSetStyle
    false,  // LivestockStatusSetBasePos
    false,  // LivestockStatusSetFoodValue
    false,  // LivestockWarpHomePos
    true,  // MapAddAreaStatusIDValueChangeEvent
    true,  // MapAddAreaStatusValueChangeEvent
    true,  // MapAddAreaStatusValueChangeEvent_StandAlone
    false,  // MapCreateFarmland
    false,  // MapCreaturePop
    false,  // MapForcedRemoveEntity
    true,  // MapObjectAddEventCallback
    true,  // MapObjectAddStatusIDValueChangeEvent
    true,  // MapObjectAddStatusTableValueChangeEvent
    true,  // MapObjectAddStatusValueChangeEvent
    false,  // MapObjectRemoveStatusIDValue
    false,  // MapObjectRemoveStatusTableValue
    false,  // MapObjectRemoveStatusValue
    false,  // MapObjectSetPos
    false,  // MapObjectSetStatusIDValue
    false,  // MapObjectSetStatusTableValue
    false,  // MapObjectSetStatusValue
    false,  // MapSetAreaStatusIDValue
    false,  // MapSetAreaStatusTableValue
    false,  // MapSetAreaStatusValue
    false,  // MapSetBushAreaFilter
    false,  // MapSetLostHouseFilter
    false,  // MapSetPassFilter
    false,  // MapSetScarecrowFlag
    false,  // MapStatusCreate
    false,  // MapStatusRemove
    false,  // MapUpdateScarecrowFlag
    true,  // MappAddAreaStatusIDValueChangeEvent
    false,  // NpcAchieveSubQuest
    false,  // NpcAddLoveRate
    false,  // NpcCheckSecretQuestVillager
    false,  // NpcCheckTalkReward
    false,  // NpcGetSubQuestRequest
    false,  // NpcSendPresent
    false,  // PlayerAddHPMax
    false,  // PlayerAddSTGaugeMax
    false,  // PlayerDamage
    false,  // PlayerHeal
    false,  // PlayerLiftingEnd
    false,  // PlayerSaveStyleBuffer
    false,  // PlayerSetConsumeEnergy
    false,  // PlayerSetHP
    false,  // PlayerSetName
    false,  // PlayerSetST
    true,  // ScriptCreate
    false,  // TaskCreate_AutoSave
    false,  // TaskCreate_GamePopupSelect
    false,  // TaskCreate_HelpPopupSelect
    false,  // TaskCreate_MainGame
    false,  // TaskCreate_SaveList
    false,  // TaskCreate_TimeSkip
    true,  // ThreadCreate
};

constexpr size_t kAllowedCount = sizeof(kAllowed) / sizeof(kAllowed[0]);
constexpr size_t kGuardedCount = sizeof(kGuarded) / sizeof(kGuarded[0]);
static_assert(sizeof(kGuardedStarts) == kGuardedCount, "one flag per guarded command");

}  // namespace lua_commands
