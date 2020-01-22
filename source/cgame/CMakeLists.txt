project (cgame)

file(GLOB CGAME_HEADES
    "*.h"
	"../gameshared/*.h"

)

file(GLOB CGAME_SOURCES
    "*.cpp"
    "../gameshared/*.c"
   	"../qalgo/rng.cpp"

)

add_library(cgame STATIC ${CGAME_SOURCES} ${CGAME_HEADES})
set_property(TARGET cgame PROPERTY CXX_STANDARD 11)
set_property(TARGET cgame PROPERTY COMPILE_DEFINITIONS "CGS")
qf_set_output_dir(cgame ${QFUSION_GAME_DIR})
