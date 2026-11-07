# CMake generated Testfile for 
# Source directory: /home/user/vocroz/apps/updater
# Build directory: /home/user/vocroz/build-tsan/apps/updater
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[pa_updater.version]=] "/home/user/vocroz/build-tsan/apps/updater/pa-updater" "--version")
set_tests_properties([=[pa_updater.version]=] PROPERTIES  PASS_REGULAR_EXPRESSION "^VOCROZ pa-updater 0.0.1" _BACKTRACE_TRIPLES "/home/user/vocroz/apps/CMakeLists.txt;15;add_test;/home/user/vocroz/apps/updater/CMakeLists.txt;4;vocroz_add_app;/home/user/vocroz/apps/updater/CMakeLists.txt;0;")
