#pragma once

namespace view
{
	extern game::cvar_t* cg_fov;
	extern game::cvar_t* cg_fovScaleEnable;
	extern game::cvar_t* cg_fovScale;

	void ready_hook_cgame_mp();
}