set(FINE_DEMO_FIXTURE "${PROJECT_SOURCE_DIR}/fine/fixtures/two-state-bisim.fine")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FINE_DEMO_FIXTURE}")
file(READ "${FINE_DEMO_FIXTURE}" FINE_DEMO_SOURCE)
configure_file(demo_source.h.in demo_source.h @ONLY)

add_executable(fine-bin
  clause_observer.cpp
  internal_rewrite.cpp
  main.cpp
  parser.cpp
  quantifier_observer.cpp
  rainfall.cpp
  runtime.cpp
  source.cpp
  synthesis.cpp
)
set_target_properties(fine-bin PROPERTIES OUTPUT_NAME fine)
target_link_libraries(fine-bin PRIVATE libz3)
foreach(define ${Z3_COMPONENT_CXX_DEFINES})
  target_compile_definitions(fine-bin PRIVATE ${define})
endforeach()
target_include_directories(fine-bin PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}"
  "${PROJECT_BINARY_DIR}/src"
  "${PROJECT_SOURCE_DIR}/src"
  "${PROJECT_SOURCE_DIR}/src/api"
)

install(TARGETS fine-bin
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(PROGRAMS "${PROJECT_SOURCE_DIR}/fine/rainfall_validate.py"
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
  RENAME fine-rain-validate
)
install(PROGRAMS "${PROJECT_SOURCE_DIR}/fine/rainfall_project.py"
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
  RENAME fine-rain-project
)
install(FILES "${PROJECT_SOURCE_DIR}/fine/rainfall_replay.py"
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES "${PROJECT_SOURCE_DIR}/fine/rainfall_projection.py"
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
