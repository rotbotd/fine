add_executable(fine-bin
  main.cpp
  parser.cpp
  proof_model_selector.cpp
  rainfall.cpp
  rainfall_lift.cpp
  runtime.cpp
  source.cpp
)
set_target_properties(fine-bin PROPERTIES OUTPUT_NAME fine)
target_link_libraries(fine-bin PRIVATE libz3)
foreach(define ${Z3_COMPONENT_CXX_DEFINES})
  target_compile_definitions(fine-bin PRIVATE ${define})
endforeach()
target_include_directories(fine-bin PRIVATE
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
