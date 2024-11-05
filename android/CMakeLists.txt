project(ReactNativeQuickSQLite)
cmake_minimum_required(VERSION 3.9.0)

set (PACKAGE_NAME "react-native-quick-sqlite")
set (CMAKE_VERBOSE_MAKEFILE ON)
set (CMAKE_CXX_STANDARD 17)
set (BUILD_DIR ${CMAKE_SOURCE_DIR}/build)

include_directories(
  ../cpp
)

add_definitions(
  -DSQLITE_TEMP_STORE=2
  ${SQLITE_FLAGS}
)

add_library(
  ${PACKAGE_NAME}
  SHARED
  ../cpp/sqliteBridge.cpp
  ../cpp/sqliteBridge.h
  ../cpp/bindings.cpp
  ../cpp/bindings.h
  ../cpp/sqlite3.h
  ../cpp/sqlite3.c
  ../cpp/JSIHelper.h
  ../cpp/JSIHelper.cpp
  ../cpp/fileUtils.h
  ../cpp/fileUtils.cpp
  ../cpp/sqliteExecute.h
  ../cpp/sqliteExecute.cpp
  ../cpp/sqlbatchexecutor.h
  ../cpp/sqlbatchexecutor.cpp
  ../cpp/macros.h
  ../cpp/ConnectionPool.cpp
  ../cpp/ConnectionPool.h
  ../cpp/ConnectionState.cpp
  ../cpp/ConnectionState.h
  cpp-adapter.cpp
)

set_target_properties(
  ${PACKAGE_NAME} PROPERTIES
  CXX_STANDARD 17
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
)

find_package(ReactAndroid REQUIRED CONFIG)
find_package(fbjni REQUIRED CONFIG)
find_package(powersync_sqlite_core REQUIRED CONFIG)

find_library(LOG_LIB log)

target_link_libraries(
  ${PACKAGE_NAME}
  ${LOG_LIB}
  fbjni::fbjni
  ReactAndroid::jsi
  android
  powersync_sqlite_core::powersync
)

if(REACTNATIVE_MERGED_SO OR ReactAndroid_VERSION_MINOR GREATER_EQUAL 76)
  target_link_libraries(
    ReactAndroid::reactnative
  )
else()
  if(${USE_HERMES})
    set(JSEXECUTOR_LIB ReactAndroid::hermes_executor)
  else()
    set(JSEXECUTOR_LIB ReactAndroid::jscexecutor)
  endif()

  target_link_libraries(
    ReactAndroid::turbomodulejsijni
    ReactAndroid::react_nativemodule_core
    ${JSEXECUTOR_LIB}
  )
endif()
