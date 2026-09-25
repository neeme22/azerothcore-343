# worldgate: pasarela cliente 3.4.3 <-> worldserver de AzerothCore (3.3.5).
add_executable(worldgate Main.cpp)
target_link_libraries(worldgate
  PRIVATE
    trinity-core-interface
  PUBLIC
    game
    mysql
    zlib)
set_target_properties(worldgate PROPERTIES FOLDER "server")
if(MSVC)
  target_link_options(worldgate PRIVATE /MAP)   # worldgate.map para situar los cuelgues
endif()

# en el repositorio los datos que lee la pasarela están en gateway/data: se copian junto al exe al compilar
set(WORLDGATE_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../data")
if(EXISTS "${WORLDGATE_DATA_DIR}/worldgate.conf.dist")
  file(GLOB WORLDGATE_DATA_FILES "${WORLDGATE_DATA_DIR}/*.txt" "${WORLDGATE_DATA_DIR}/*.csv" "${WORLDGATE_DATA_DIR}/*.dist")
  add_custom_command(TARGET worldgate POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${WORLDGATE_DATA_FILES} $<TARGET_FILE_DIR:worldgate>)
endif()
