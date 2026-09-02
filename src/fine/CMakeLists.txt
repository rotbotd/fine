set(FINE_DEMO_FIXTURE "${PROJECT_SOURCE_DIR}/fine/fixtures/two-state-bisim.fine")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FINE_DEMO_FIXTURE}")
file(READ "${FINE_DEMO_FIXTURE}" FINE_DEMO_SOURCE)
configure_file(demo_source.h.in demo_source.h @ONLY)

add_executable(fine-bin
  bisimulation_runtime.cpp
  clause_observer.cpp
  fixedpoint_observer.cpp
  internal_rewrite.cpp
  main.cpp
  parser.cpp
  quantifier_observer.cpp
  predicate_runtime.cpp
  rainfall.cpp
  rainfall_lift.cpp
  runtime.cpp
  source.cpp
  synthesis.cpp
  synthesis_runtime.cpp
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
install(PROGRAMS "${PROJECT_SOURCE_DIR}/fine/rainfall_generation_cli.py"
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
  RENAME fine-rain-generation
)
install(FILES "${PROJECT_SOURCE_DIR}/fine/rainfall_generation.py"
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(PROGRAMS "${PROJECT_SOURCE_DIR}/fine/rainfall_host_cli.py"
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
  RENAME fine-rain-host
)
install(FILES "${PROJECT_SOURCE_DIR}/fine/rainfall_host.py"
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(PROGRAMS "${PROJECT_SOURCE_DIR}/fine/rainfall_live_cli.py"
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
  RENAME fine-rain-live
)
install(FILES "${PROJECT_SOURCE_DIR}/fine/rainfall_live.py"
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
