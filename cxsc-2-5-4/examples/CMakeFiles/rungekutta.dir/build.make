# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.5

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/anhvt89/Documents/c++lib/cxsc-2-5-4

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/anhvt89/Documents/c++lib/cxsc-2-5-4

# Include any dependencies generated for this target.
include examples/CMakeFiles/rungekutta.dir/depend.make

# Include the progress variables for this target.
include examples/CMakeFiles/rungekutta.dir/progress.make

# Include the compile flags for this target's objects.
include examples/CMakeFiles/rungekutta.dir/flags.make

examples/CMakeFiles/rungekutta.dir/rungekutta.o: examples/CMakeFiles/rungekutta.dir/flags.make
examples/CMakeFiles/rungekutta.dir/rungekutta.o: examples/rungekutta.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/anhvt89/Documents/c++lib/cxsc-2-5-4/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object examples/CMakeFiles/rungekutta.dir/rungekutta.o"
	cd /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples && /usr/bin/c++   $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/rungekutta.dir/rungekutta.o -c /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples/rungekutta.cpp

examples/CMakeFiles/rungekutta.dir/rungekutta.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/rungekutta.dir/rungekutta.i"
	cd /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples && /usr/bin/c++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples/rungekutta.cpp > CMakeFiles/rungekutta.dir/rungekutta.i

examples/CMakeFiles/rungekutta.dir/rungekutta.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/rungekutta.dir/rungekutta.s"
	cd /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples && /usr/bin/c++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples/rungekutta.cpp -o CMakeFiles/rungekutta.dir/rungekutta.s

examples/CMakeFiles/rungekutta.dir/rungekutta.o.requires:

.PHONY : examples/CMakeFiles/rungekutta.dir/rungekutta.o.requires

examples/CMakeFiles/rungekutta.dir/rungekutta.o.provides: examples/CMakeFiles/rungekutta.dir/rungekutta.o.requires
	$(MAKE) -f examples/CMakeFiles/rungekutta.dir/build.make examples/CMakeFiles/rungekutta.dir/rungekutta.o.provides.build
.PHONY : examples/CMakeFiles/rungekutta.dir/rungekutta.o.provides

examples/CMakeFiles/rungekutta.dir/rungekutta.o.provides.build: examples/CMakeFiles/rungekutta.dir/rungekutta.o


# Object files for target rungekutta
rungekutta_OBJECTS = \
"CMakeFiles/rungekutta.dir/rungekutta.o"

# External object files for target rungekutta
rungekutta_EXTERNAL_OBJECTS =

examples/rungekutta: examples/CMakeFiles/rungekutta.dir/rungekutta.o
examples/rungekutta: examples/CMakeFiles/rungekutta.dir/build.make
examples/rungekutta: libcxsc.so.2.5.4
examples/rungekutta: examples/CMakeFiles/rungekutta.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/anhvt89/Documents/c++lib/cxsc-2-5-4/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable rungekutta"
	cd /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/rungekutta.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
examples/CMakeFiles/rungekutta.dir/build: examples/rungekutta

.PHONY : examples/CMakeFiles/rungekutta.dir/build

examples/CMakeFiles/rungekutta.dir/requires: examples/CMakeFiles/rungekutta.dir/rungekutta.o.requires

.PHONY : examples/CMakeFiles/rungekutta.dir/requires

examples/CMakeFiles/rungekutta.dir/clean:
	cd /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples && $(CMAKE_COMMAND) -P CMakeFiles/rungekutta.dir/cmake_clean.cmake
.PHONY : examples/CMakeFiles/rungekutta.dir/clean

examples/CMakeFiles/rungekutta.dir/depend:
	cd /home/anhvt89/Documents/c++lib/cxsc-2-5-4 && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/anhvt89/Documents/c++lib/cxsc-2-5-4 /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples /home/anhvt89/Documents/c++lib/cxsc-2-5-4 /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples/CMakeFiles/rungekutta.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : examples/CMakeFiles/rungekutta.dir/depend

