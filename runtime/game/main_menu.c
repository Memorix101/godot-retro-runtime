/* MainMenu: reacts to the menu buttons. Placeholder script: components/main_menu.gd. */
#include "../core/rr_scene.h"
#include "game_state.h"

typedef struct {
    const char *start_scene;         /* exported */
    const char *test_2d_scene;       /* exported */
    const char *platformer_2d_scene; /* exported */
    const char *platformer_3d_scene; /* exported */
    const char *racing_scene;        /* exported */
    const char *island_scene;        /* exported */
    const char *ball_scene;          /* exported */
    const char *feature_scene;       /* exported */
} MainMenu;

static void main_menu_on_start(Node *self, MainMenu *menu)
{
    (void)self;
    rr_change_scene(menu->start_scene);
}

static void main_menu_on_test_2d(Node *self, MainMenu *menu)
{
    (void)self;
    rr_change_scene(menu->test_2d_scene);
}

static void main_menu_on_platformer_2d(Node *self, MainMenu *menu)
{
    (void)self;
    rr_change_scene(menu->platformer_2d_scene);
}

static void main_menu_on_platformer_3d(Node *self, MainMenu *menu)
{
    (void)self;
    rr_change_scene(menu->platformer_3d_scene);
}

static void main_menu_on_racing(Node *self, MainMenu *menu)
{
    (void)self;
    rr_change_scene(menu->racing_scene);
}

static void main_menu_on_island(Node *self, MainMenu *menu)
{
    (void)self;
    rr_change_scene(menu->island_scene);
}

static void main_menu_on_ball(Node *self, MainMenu *menu)
{
    (void)self;
    rr_change_scene(menu->ball_scene);
}

static void main_menu_on_features(Node *self, MainMenu *menu)
{
    GameState *game = game_state(self->scene);
    if (game != NULL) {
        game->from_menu = true; /* the scene stays open and shows its report */
    }
    rr_change_scene(menu->feature_scene);
}

static void main_menu_on_quit(Node *self, MainMenu *menu)
{
    (void)self;
    (void)menu;
    rr_quit();
}

COMPONENT_BEGIN(MainMenu, "MainMenu")
    EXPORT_SCENE(start_scene)
    EXPORT_SCENE(test_2d_scene)
    EXPORT_SCENE(platformer_2d_scene)
    EXPORT_SCENE(platformer_3d_scene)
    EXPORT_SCENE(racing_scene)
    EXPORT_SCENE(island_scene)
    EXPORT_SCENE(ball_scene)
    EXPORT_SCENE(feature_scene)
    ON_SIGNAL("_on_start_pressed", main_menu_on_start)
    ON_SIGNAL("_on_test_2d_pressed", main_menu_on_test_2d)
    ON_SIGNAL("_on_platformer_2d_pressed", main_menu_on_platformer_2d)
    ON_SIGNAL("_on_platformer_3d_pressed", main_menu_on_platformer_3d)
    ON_SIGNAL("_on_racing_pressed", main_menu_on_racing)
    ON_SIGNAL("_on_island_pressed", main_menu_on_island)
    ON_SIGNAL("_on_ball_pressed", main_menu_on_ball)
    ON_SIGNAL("_on_features_pressed", main_menu_on_features)
    ON_SIGNAL("_on_quit_pressed", main_menu_on_quit)
COMPONENT_END()
