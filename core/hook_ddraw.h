#pragma once

namespace pacer {

// Hooks DirectDraw surface Flip/Blt (vtable idx 11/5) so legacy DDraw titles
// are paced too. NOTE (M4): code is build-validated only; no live DDraw game
// was on hand to exercise the surface path at build time.
bool hook_ddraw_install();

}  // namespace pacer