# CMake generated Testfile for 
# Source directory: /home/user/vocroz/apps/pa-desktop
# Build directory: /home/user/vocroz/build-tsan/apps/pa-desktop
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[remote_pa.version]=] "/home/user/vocroz/build-tsan/apps/pa-desktop/remote-pa" "--version")
set_tests_properties([=[remote_pa.version]=] PROPERTIES  PASS_REGULAR_EXPRESSION "^VOCROZ remote-pa 0.0.1" _BACKTRACE_TRIPLES "/home/user/vocroz/apps/CMakeLists.txt;15;add_test;/home/user/vocroz/apps/pa-desktop/CMakeLists.txt;4;vocroz_add_app;/home/user/vocroz/apps/pa-desktop/CMakeLists.txt;0;")
