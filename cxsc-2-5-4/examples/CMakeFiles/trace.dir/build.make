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
include examples/CMakeFiles/trace.dir/depend.make

# Include the progress variables for this target.
include examples/CMakeFiles/trace.dir/progress.make

# Include the compile flags for this target's objects.
include examples/CMakeFiles/trace.dir/flags.make

examples/CMakeFiles/trace.dir/trace.o: examples/CMakeFiles/trace.dir/flags.make
examples/CMakeFiles/trace.dir/trace.o: examples/trace.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/anhvt89/Documents/c++lib/cxsc-2-5-4/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object examples/CMakeFiles/trace.dir/trace.o"
	cd /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples && /usr/bin/c++   $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/trace.dir/trace.o -c /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples/trace.cpp

examples/CMakeFiles/trace.dir/trace.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/trace.dir/trace.i"
	cd /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples && /usr/bin/c++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples/trace.cpp > CMakeFiles/trace.dir/trace.i

examples/CMakeFiles/trace.dir/trace.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/trace.dir/trace.s"
	cd /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples && /usr/bin/c++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples/trace.cpp -o CMakeFiles/trace.dir/trace.s

examples/CMakeFiles/trace.dir/trace.o.requires:

.PHONY : examples/CMakeFiles/trace.dir/trace.o.requires

examples/CMakeFiles/trace.dir/trace.o.provides: examples/CMakeFiles/trace.dir/trace.o.requires
	$(MAKE) -f examples/CMakeFiles/trace.dir/build.make examples/CMakeFiles/trace.dir/trace.o.provides.build
.PHONY : examples/CMakeFiles/trace.dir/trace.o.provides

examples/CMakeFiles/trace.dir/trace.o.provides.build: examples/CMakeFiles/trace.dir/trace.o


# Object files for target trace
trace_OBJECTS = \
"CMakeFiles/trace.dir/trace.o"

# External object files for target trace
trace_EXTERNAL_OBJECTS =

examples/trace: examples/CMakeFiles/trace.dir/trace.o
examples/trace: examples/CMakeFiles/trace.dir/build.make
examples/trace: libcxsc.so.2.5.4
examples/trace: examples/CMakeFiles/trace.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/anhvt89/Documents/c++lib/cxsc-2-5-4/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable trace"
	cd /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/trace.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
examples/CMakeFiles/trace.dir/build: examples/trace

.PHONY : examples/CMakeFiles/trace.dir/build

examples/CMakeFiles/trace.dir/requires: examples/CMakeFiles/trace.dir/trace.o.requires

.PHONY : examples/CMakeFiles/trace.dir/requires

examples/CMakeFiles/trace.dir/clean:
	cd /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples && $(CMAKE_COMMAND) -P CMakeFiles/trace.dir/cmake_clean.cmake
.PHONY : examples/CMakeFiles/trace.dir/clean

examples/CMakeFiles/trace.dir/depend:
	cd /home/anhvt89/Documents/c++lib/cxsc-2-5-4 && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/anhvt89/Documents/c++lib/cxsc-2-5-4 /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples /home/anhvt89/Documents/c++lib/cxsc-2-5-4 /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples /home/anhvt89/Documents/c++lib/cxsc-2-5-4/examples/CMakeFiles/trace.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : examples/CMakeFiles/trace.dir/depend
