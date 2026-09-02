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
target_compile_options(fine-bin PRIVATE ${Z3_COMPONENT_CXX_FLAGS})
foreach(define ${Z3_COMPONENT_CXX_DEFINES})
  target_compile_definitions(fine-bin PRIVATE ${define})
endforeach()
target_include_directories(fine-bin PRIVATE
  "${PROJECT_BINARY_DIR}/src"
  "${PROJECT_SOURCE_DIR}/src"
  "${PROJECT_SOURCE_DIR}/src/api"
)

if (EMSCRIPTEN)
  # The browser playground drives the ordinary CLI through Emscripten's MEMFS.
  # Keep one executable boundary rather than growing a second, browser-only
  # interpretation of Fine.
  set_target_properties(fine-bin PROPERTIES SUFFIX ".mjs")
  target_link_options(fine-bin PRIVATE
    "-fwasm-exceptions"
    "-sSUPPORT_LONGJMP=wasm"
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sEXPORT_NAME=createFine"
    "-sEXPORTED_RUNTIME_METHODS=['FS','callMain']"
    "-sINVOKE_RUN=0"
    "-sEXIT_RUNTIME=0"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sENVIRONMENT=web,node"
  )
endif()

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
