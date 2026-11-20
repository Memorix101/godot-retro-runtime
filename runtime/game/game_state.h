/* GameState: the component of the autoload "Game". Placeholder: components/game_state.gd. */
#ifndef GAME_STATE_H
#define GAME_STATE_H

#include "../core/rr_scene.h"

typedef struct {
    int32_t start_lives; /* exported */

    int32_t visits;      /* scenes loaded since the program started */
    int32_t marker;        /* the feature test: set while it loads itself a second time */
    int32_t marker_visits; /* the feature test: `visits` when it set the mark */
    bool    from_menu;     /* the feature test was started from the menu and stays open */
} GameState;

/* The state from anywhere in the game. NULL if the project has no "Game" autoload. */
static inline GameState *game_state(RRScene *scene)
{
    return (GameState *)rr_node_component(rr_autoload(scene, "Game"), "GameState");
}

#endif
