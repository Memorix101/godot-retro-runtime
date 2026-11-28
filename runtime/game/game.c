/* The list of game components. Add every new component here. */
#include "game.h"

#include "../core/rr_component.h"

COMPONENT_DECLARE(MainMenu)
COMPONENT_DECLARE(PlayerCar)
COMPONENT_DECLARE(Ship2D)
COMPONENT_DECLARE(Bullet2D)
COMPONENT_DECLARE(Spinner2D)
COMPONENT_DECLARE(PlatformPlayer2D)
COMPONENT_DECLARE(PlatformPlayer3D)
COMPONENT_DECLARE(PathPatrol)
COMPONENT_DECLARE(RaceCar)
COMPONENT_DECLARE(AiDriver)
COMPONENT_DECLARE(CrumblePlatform)
COMPONENT_DECLARE(BallRoller)
COMPONENT_DECLARE(StuntCar)
COMPONENT_DECLARE(StarterVehicle)
COMPONENT_DECLARE(StarterView)
COMPONENT_DECLARE(FeatureTest)
COMPONENT_DECLARE(TriggerStress)
COMPONENT_DECLARE(UiTest)
COMPONENT_DECLARE(TileTest)
COMPONENT_DECLARE(CanvasTest)
COMPONENT_DECLARE(ScrollTest)
COMPONENT_DECLARE(AnimTest)
COMPONENT_DECLARE(GridTest)
COMPONENT_DECLARE(Rigid2DTest)
COMPONENT_DECLARE(CastTest)
COMPONENT_DECLARE(ConcaveTest)
COMPONENT_DECLARE(SkinTest)
COMPONENT_DECLARE(TestStats)
COMPONENT_DECLARE(GameState)
COMPONENT_DECLARE(Collectable)
COMPONENT_DECLARE(AirplaneCamera)
COMPONENT_DECLARE(AirplanePlayer)
COMPONENT_DECLARE(AirplaneBullet)
COMPONENT_DECLARE(AirplaneGame)
COMPONENT_DECLARE(Jukebox)
COMPONENT_DECLARE(ChaseCamera)
COMPONENT_DECLARE(HighwayPlayer)
COMPONENT_DECLARE(HighwayCar)
COMPONENT_DECLARE(HighwayRival)
COMPONENT_DECLARE(HighwayRoad)
COMPONENT_DECLARE(TitleScreen)
COMPONENT_DECLARE(ForwardDummy)
COMPONENT_DECLARE(HighwayWreck)
COMPONENT_DECLARE(Flare)
COMPONENT_DECLARE(NeonGame)
COMPONENT_DECLARE(NeonCamera)
COMPONENT_DECLARE(NeonPlayer)
COMPONENT_DECLARE(NeonEnemy)
COMPONENT_DECLARE(NeonLaser)
COMPONENT_DECLARE(NeonBomb)
COMPONENT_DECLARE(NeonSpawner)
COMPONENT_DECLARE(NeonDisco)
COMPONENT_DECLARE(NeonFlicker)
COMPONENT_DECLARE(NeonMenu)
COMPONENT_DECLARE(SpelunkyTitle)
COMPONENT_DECLARE(SpelunkyLevel)

bool game_register_components(void)
{
    return rr_component_register(&MainMenu_component) &&
           rr_component_register(&PlayerCar_component) &&
           rr_component_register(&Ship2D_component) &&
           rr_component_register(&Bullet2D_component) &&
           rr_component_register(&Spinner2D_component) &&
           rr_component_register(&PlatformPlayer2D_component) &&
           rr_component_register(&PlatformPlayer3D_component) &&
           rr_component_register(&PathPatrol_component) &&
           rr_component_register(&RaceCar_component) &&
           rr_component_register(&AiDriver_component) &&
           rr_component_register(&CrumblePlatform_component) &&
           rr_component_register(&BallRoller_component) &&
           rr_component_register(&StuntCar_component) &&
           rr_component_register(&StarterVehicle_component) &&
           rr_component_register(&StarterView_component) &&
           rr_component_register(&FeatureTest_component) &&
           rr_component_register(&TriggerStress_component) &&
           rr_component_register(&UiTest_component) &&
           rr_component_register(&TileTest_component) &&
           rr_component_register(&CanvasTest_component) &&
           rr_component_register(&ScrollTest_component) &&
           rr_component_register(&AnimTest_component) &&
           rr_component_register(&GridTest_component) &&
           rr_component_register(&Rigid2DTest_component) &&
           rr_component_register(&CastTest_component) &&
           rr_component_register(&ConcaveTest_component) &&
           rr_component_register(&SkinTest_component) &&
           rr_component_register(&TestStats_component) &&
           rr_component_register(&GameState_component) &&
           rr_component_register(&Collectable_component) &&
           rr_component_register(&AirplaneCamera_component) &&
           rr_component_register(&AirplanePlayer_component) &&
           rr_component_register(&AirplaneBullet_component) &&
           rr_component_register(&AirplaneGame_component) &&
           rr_component_register(&Jukebox_component) &&
           rr_component_register(&ChaseCamera_component) &&
           rr_component_register(&HighwayPlayer_component) &&
           rr_component_register(&HighwayCar_component) &&
           rr_component_register(&HighwayRival_component) &&
           rr_component_register(&HighwayRoad_component) &&
           rr_component_register(&TitleScreen_component) &&
           rr_component_register(&ForwardDummy_component) &&
           rr_component_register(&HighwayWreck_component) &&
           rr_component_register(&Flare_component) &&
           rr_component_register(&NeonGame_component) &&
           rr_component_register(&NeonCamera_component) &&
           rr_component_register(&NeonPlayer_component) &&
           rr_component_register(&NeonEnemy_component) &&
           rr_component_register(&NeonLaser_component) &&
           rr_component_register(&NeonBomb_component) &&
           rr_component_register(&NeonSpawner_component) &&
           rr_component_register(&NeonDisco_component) &&
           rr_component_register(&NeonFlicker_component) &&
           rr_component_register(&NeonMenu_component) &&
           rr_component_register(&SpelunkyTitle_component) &&
           rr_component_register(&SpelunkyLevel_component);
}
