cmake_minimum_required(VERSION 3.20)
project(tuiman VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Curses REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)

add_executable(tuiman
  src/main.c
  src/core/paths.c
  src/core/editor.c
  src/store/request_store.c
  src/store/history_store.c
  src/store/export_import.c
  src/net/http_client.c
  src/auth/keychain_macos.c
)

target_include_directories(tuiman PRIVATE
  include
  ${CURSES_INCLUDE_DIR}
)

target_link_libraries(tuiman PRIVATE
  ${CURSES_LIBRARIES}
  SQLite::SQLite3
  CURL::libcurl
)

if(APPLE)
  target_compile_definitions(tuiman PRIVATE TUIMAN_PLATFORM_MACOS=1)
endif()

install(TARGETS tuiman DESTINATION bin)
