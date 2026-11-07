# CMake generated Testfile for 
# Source directory: /home/user/vocroz/apps/pa-engine
# Build directory: /home/user/vocroz/build-tsan/apps/pa-engine
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[pa_engine.version]=] "/home/user/vocroz/build-tsan/apps/pa-engine/pa-engine" "--version")
set_tests_properties([=[pa_engine.version]=] PROPERTIES  PASS_REGULAR_EXPRESSION "^VOCROZ pa-engine 0.0.1" _BACKTRACE_TRIPLES "/home/user/vocroz/apps/CMakeLists.txt;15;add_test;/home/user/vocroz/apps/pa-engine/CMakeLists.txt;4;vocroz_add_app;/home/user/vocroz/apps/pa-engine/CMakeLists.txt;0;")
