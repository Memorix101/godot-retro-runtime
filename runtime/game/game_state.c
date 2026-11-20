#include "game_state.h"

static void game_state_ready(Node *self, GameState *state)
{
    (void)self;
    state->visits++;
}

COMPONENT_BEGIN(GameState, "GameState")
    EXPORT_INT(start_lives)
    ON_READY(game_state_ready)
COMPONENT_END()
