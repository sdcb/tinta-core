#ifndef TINTA_FEATURES_H
#define TINTA_FEATURES_H

/*
 * CMake defines these switches for normal builds.  The defaults keep direct
 * source builds compatible with the complete Tinta Core feature set.
 */
#ifndef TINTA_ENABLE_UIA
#define TINTA_ENABLE_UIA 1
#endif

#ifndef TINTA_ENABLE_MERMAID
#define TINTA_ENABLE_MERMAID 1
#endif

#ifndef TINTA_ENABLE_SYNTAX
#define TINTA_ENABLE_SYNTAX 1
#endif

#ifndef TINTA_ENABLE_REMOTE_IMAGES
#define TINTA_ENABLE_REMOTE_IMAGES 1
#endif

#ifndef TINTA_ENABLE_LOCAL_IMAGES
#define TINTA_ENABLE_LOCAL_IMAGES 1
#endif

#define TINTA_ENABLE_IMAGES \
    (TINTA_ENABLE_REMOTE_IMAGES || TINTA_ENABLE_LOCAL_IMAGES)

#endif
