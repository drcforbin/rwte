#ifndef RWTE_BUILDOPT_H
#define RWTE_BUILDOPT_H

#if defined(RWTE_NO_WAYLAND) && defined(RWTE_NO_XCB)
    #error cannot define both RWTE_NO_WAYLAND and RWTE_NO_XCB
#elif defined(RWTE_NO_XCB)
    #define BUILD_WAYLAND_ONLY
    const bool build_wayland_optional = false;
    const bool build_xcb_only = false;
    const bool build_wayland_only = true;
#elif defined(RWTE_NO_WAYLAND)
    #define BUILD_XCB_ONLY
    const bool build_wayland_optional = false;
    const bool build_xcb_only = true;
    const bool build_wayland_only = false;
#else
    #define BUILD_WAYLAND_OPTIONAL
    const bool build_wayland_optional = true;
    const bool build_xcb_only = false;
    const bool build_wayland_only = false;
#endif

#endif // RWTE_BUILDOPT_H
