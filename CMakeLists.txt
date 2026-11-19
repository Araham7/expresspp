cmake_minimum_required(VERSION 3.16)
project(expresspp VERSION 0.4.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(EXPRESSPP_TOP ON)
else()
  set(EXPRESSPP_TOP OFF)
endif()
option(EXPRESSPP_BUILD_TESTS "Build Express++ tests" ${EXPRESSPP_TOP})
option(EXPRESSPP_BUILD_EXAMPLES "Build Express++ examples" ${EXPRESSPP_TOP})

find_package(Drogon CONFIG REQUIRED)
find_package(nevla CONFIG QUIET)   # optional: enables expresspp::nevla and the nevla example/tests

# ---- expresspp::env  (dotenv; no Drogon needed) ----
add_library(expresspp_env src/env.cc)
add_library(expresspp::env ALIAS expresspp_env)
target_include_directories(expresspp_env PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)

# ---- expresspp::core  (App, Router, Req, Res, json, views, serveStatic, json()/bodyLimit) ----
add_library(expresspp_core src/app.cc src/req.cc src/res.cc src/json.cc src/view.cc src/body.cc src/static.cc)
add_library(expresspp::core ALIAS expresspp_core)
target_include_directories(expresspp_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(expresspp_core PUBLIC Drogon::Drogon expresspp_env)

# ---- packages on top of core ----
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
pkg_check_modules(XCRYPT REQUIRED IMPORTED_TARGET libxcrypt)

# security: jwt + auth, hash (argon2/bcrypt), validate, helmet, rateLimit, upload; offload() worker pool
add_library(expresspp_security src/offload.cc src/crypto.cc src/cookie_parser.cc src/jwt.cc src/hash.cc src/validate.cc src/helmet.cc src/rate_limit.cc src/upload.cc)
add_library(expresspp::security ALIAS expresspp_security)
target_link_libraries(expresspp_security PUBLIC expresspp_core PRIVATE OpenSSL::Crypto PkgConfig::SODIUM PkgConfig::XCRYPT)

add_library(expresspp_logger src/logger.cc)
add_library(expresspp::logger ALIAS expresspp_logger)
target_link_libraries(expresspp_logger PUBLIC expresspp_core)

add_library(expresspp_cors src/cors.cc)
add_library(expresspp::cors ALIAS expresspp_cors)
target_link_libraries(expresspp_cors PUBLIC expresspp_core)

# ---- expresspp::expresspp  (everything above) ----
add_library(expresspp INTERFACE)
add_library(expresspp::expresspp ALIAS expresspp)
target_link_libraries(expresspp INTERFACE expresspp_core expresspp_env expresspp_logger expresspp_cors expresspp_security)

# ---- expresspp::nevla  (glue, header-only) ----
if(nevla_FOUND)
  add_library(expresspp_nevla INTERFACE)
  add_library(expresspp::nevla ALIAS expresspp_nevla)
  target_link_libraries(expresspp_nevla INTERFACE expresspp_core nevla::nevla)
  message(STATUS "Express++: nevla ${nevla_VERSION} found, expresspp::nevla enabled")
else()
  message(STATUS "Express++: nevla not found, expresspp::nevla disabled")
endif()

foreach(t expresspp_env expresspp_core expresspp_logger expresspp_cors expresspp_security)
  target_compile_options(${t} PRIVATE -Wall -Wextra)
endforeach()
# installed names match the in-tree aliases: expresspp::core, expresspp::env, ..., expresspp::nevla
foreach(part env core logger cors security)
  set_target_properties(expresspp_${part} PROPERTIES EXPORT_NAME ${part})
endforeach()
set_target_properties(expresspp PROPERTIES EXPORT_NAME expresspp)
if(nevla_FOUND)
  set_target_properties(expresspp_nevla PROPERTIES EXPORT_NAME nevla)
endif()

if(EXPRESSPP_BUILD_TESTS)
  enable_testing()
  add_executable(expresspp_tests tests/test_express.cc)
  target_link_libraries(expresspp_tests PRIVATE expresspp::expresspp)
  if(nevla_FOUND)
    target_link_libraries(expresspp_tests PRIVATE expresspp::nevla)
    target_compile_definitions(expresspp_tests PRIVATE EXPRESSPP_TEST_NEVLA=1)
  endif()
  add_test(NAME expresspp_tests COMMAND expresspp_tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

if(EXPRESSPP_BUILD_EXAMPLES)
  add_executable(01_hello examples/01_hello.cc)
  target_link_libraries(01_hello PRIVATE expresspp::expresspp)
  add_executable(03_website examples/03_website.cc)
  target_link_libraries(03_website PRIVATE expresspp::expresspp)
  add_executable(04_auth_api examples/04_auth_api.cc)
  target_link_libraries(04_auth_api PRIVATE expresspp::expresspp)
  if(nevla_FOUND)
    add_executable(02_users_api examples/02_users_api.cc)
    target_link_libraries(02_users_api PRIVATE expresspp::expresspp expresspp::nevla)
  endif()
endif()

include(GNUInstallDirs)
install(TARGETS expresspp_env expresspp_core expresspp_logger expresspp_cors expresspp_security expresspp EXPORT expressppTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/express DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT expressppTargets NAMESPACE expresspp:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/expresspp)
# the nevla glue gets its own export set: it is only loaded when nevla is installed too
if(nevla_FOUND)
  install(TARGETS expresspp_nevla EXPORT expressppNevlaTargets)
  install(EXPORT expressppNevlaTargets NAMESPACE expresspp:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/expresspp)
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/expressppConfig.cmake
"include(CMakeFindDependencyMacro)\nif(NOT TARGET Drogon::Drogon)\n  find_dependency(Drogon)\nendif()\nfind_dependency(OpenSSL)\nfind_dependency(PkgConfig)\npkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)\npkg_check_modules(XCRYPT REQUIRED IMPORTED_TARGET libxcrypt)\ninclude(\${CMAKE_CURRENT_LIST_DIR}/expressppTargets.cmake)\n"
"if(EXISTS \${CMAKE_CURRENT_LIST_DIR}/expressppNevlaTargets.cmake)\n  find_package(nevla CONFIG QUIET)\n  if(nevla_FOUND)\n    include(\${CMAKE_CURRENT_LIST_DIR}/expressppNevlaTargets.cmake)\n  endif()\nendif()\n")
include(CMakePackageConfigHelpers)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/expressppConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/expressppConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/expressppConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/expresspp)
