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
COMPONENT_DECLARE(SkinTest)
COMPONENT_DECLARE(TestStats)
COMPONENT_DECLARE(GameState)
COMPONENT_DECLARE(Collectable)
COMPONENT_DECLARE(AirplaneCamera)
COMPONENT_DECLARE(AirplanePlayer)
COMPONENT_DECLARE(AirplaneBullet)
COMPONENT_DECLARE(AirplaneGame)
COMPONENT_DECLARE(Jukebox)

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
           rr_component_register(&SkinTest_component) &&
           rr_component_register(&TestStats_component) &&
           rr_component_register(&GameState_component) &&
           rr_component_register(&Collectable_component) &&
           rr_component_register(&AirplaneCamera_component) &&
           rr_component_register(&AirplanePlayer_component) &&
           rr_component_register(&AirplaneBullet_component) &&
           rr_component_register(&AirplaneGame_component) &&
           rr_component_register(&Jukebox_component);
}
