add_executable(fine-bin
  main.cpp
  parser.cpp
  proof_model_selector.cpp
  rainfall.cpp
  rainfall_lift.cpp
  runtime.cpp
  document_runner.cpp
  value_definition_plan.cpp
  value_recursion.cpp
  proof_engine_inductive.cpp
  proof_engine_search.cpp
  proof_engine_types.cpp
  value_counterexample.cpp
  value_elaborator.cpp
  value_flow.cpp
  stage_analysis.cpp
  stage_evaluation.cpp
  stage_transfer.cpp
  stage_analysis_probe.cpp
  source.cpp
)
option(FINE_ENABLE_LIVE_LIFT "Build the nonblocking live-lifting prototype" OFF)
if (NOT EMSCRIPTEN OR FINE_ENABLE_LIVE_LIFT)
  target_sources(fine-bin PRIVATE
    browser_live.cpp
    live_lift.cpp
    live_lift_probe.cpp
  )
  target_compile_definitions(fine-bin PRIVATE FINE_HAS_LIVE_LIFT=1)
endif()
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
    "-sEXPORTED_RUNTIME_METHODS=['FS','callMain','HEAP8']"
    "-sINVOKE_RUN=0"
    "-sEXIT_RUNTIME=0"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sENVIRONMENT=web,node"
  )
  if (FINE_ENABLE_LIVE_LIFT)
    target_compile_options(fine-bin PRIVATE "-pthread")
    target_link_options(fine-bin PRIVATE
      "-pthread"
      "-sPTHREAD_POOL_SIZE=2"
    )
  endif()
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
