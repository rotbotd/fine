set(FINE_DEMO_FIXTURE "${PROJECT_SOURCE_DIR}/fine/fixtures/two-state-bisim.fine")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FINE_DEMO_FIXTURE}")
file(READ "${FINE_DEMO_FIXTURE}" FINE_DEMO_SOURCE)
configure_file(demo_source.h.in demo_source.h @ONLY)

add_executable(fine-bin
  main.cpp
  parser.cpp
  rainfall.cpp
  runtime.cpp
  synthesis.cpp
)
set_target_properties(fine-bin PROPERTIES OUTPUT_NAME fine)
target_link_libraries(fine-bin PRIVATE libz3)
target_include_directories(fine-bin PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}"
  "${PROJECT_SOURCE_DIR}/src/api"
)

install(TARGETS fine-bin
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
