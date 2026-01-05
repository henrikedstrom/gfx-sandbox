# MikkTSpace - tangent space generation library
# https://github.com/mmikk/MikkTSpace

add_library(mikktspace STATIC
  mikktspace.c
  mikktspace.h
)

target_include_directories(mikktspace PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# Silence warnings for third-party C code
if(MSVC)
  target_compile_options(mikktspace PRIVATE /W0)
else()
  target_compile_options(mikktspace PRIVATE -w)
endif()

set_target_properties(mikktspace PROPERTIES FOLDER "ThirdParty/MikkTSpace")
