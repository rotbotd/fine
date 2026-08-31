add_executable(fine-bin main.cpp)
set_target_properties(fine-bin PROPERTIES OUTPUT_NAME fine)
target_link_libraries(fine-bin PRIVATE libz3)
target_include_directories(fine-bin PRIVATE "${PROJECT_SOURCE_DIR}/src/api")

install(TARGETS fine-bin
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
