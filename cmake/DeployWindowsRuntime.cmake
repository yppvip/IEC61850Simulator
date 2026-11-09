# Run on every default build, including when the executable did not relink.
cmake_minimum_required(VERSION 3.21)
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()
foreach(RequiredVariable ApplicationPath QtDeployTool QtRuntimeDirectory CompilerRuntimeDirectory)
    if(NOT DEFINED ${RequiredVariable})
        message(FATAL_ERROR "Missing ${RequiredVariable}")
    endif()
endforeach()

# windeployqt and its helpers must also resolve their own runtime libraries.
set(ENV{PATH} "${QtRuntimeDirectory};${CompilerRuntimeDirectory};$ENV{PATH}")
execute_process(COMMAND "${QtDeployTool}" --no-translations "${ApplicationPath}"
    RESULT_VARIABLE DeployResult)
if(NOT DeployResult EQUAL 0)
    message(FATAL_ERROR "windeployqt failed: ${DeployResult}")
endif()

get_filename_component(RuntimeDirectory "${ApplicationPath}" DIRECTORY)
file(GLOB_RECURSE DeployedLibraries "${RuntimeDirectory}/*.dll")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
if(CMAKE_OBJDUMP)
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${CMAKE_OBJDUMP}")
endif()
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${ApplicationPath}"
    LIBRARIES ${DeployedLibraries}
    DIRECTORIES "${RuntimeDirectory}" "${QtRuntimeDirectory}" "${CompilerRuntimeDirectory}"
    RESOLVED_DEPENDENCIES_VAR ResolvedDependencies
    UNRESOLVED_DEPENDENCIES_VAR UnresolvedDependencies
    CONFLICTING_DEPENDENCIES_PREFIX ConflictingDependencies
    PRE_EXCLUDE_REGEXES "[Aa][Pp][Ii]-[Mm][Ss]-.*" "[Ee][Xx][Tt]-[Mm][Ss]-.*"
    POST_EXCLUDE_REGEXES ".*[/\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\].*"
        ".*[/\\][Ss][Yy][Ss][Ww][Oo][Ww]64[/\\].*")
if(UnresolvedDependencies)
    message(FATAL_ERROR "Unresolved runtime DLLs: ${UnresolvedDependencies}")
endif()
# A previously deployed DLL and its toolchain original may both be found.
# Refresh that DLL from the selected toolchain; reject unrelated conflicts.
foreach(DependencyName IN LISTS ConflictingDependencies_FILENAMES)
    set(PreferredDependency "")
    foreach(SourceDirectory "${QtRuntimeDirectory}" "${CompilerRuntimeDirectory}")
        set(Candidate "${SourceDirectory}/${DependencyName}")
        if(Candidate IN_LIST ConflictingDependencies_${DependencyName})
            set(PreferredDependency "${Candidate}")
            break()
        endif()
    endforeach()
    if(NOT PreferredDependency)
        message(FATAL_ERROR "Conflicting runtime DLL ${DependencyName}: ${ConflictingDependencies_${DependencyName}}")
    endif()
    list(APPEND ResolvedDependencies "${PreferredDependency}")
endforeach()
foreach(Dependency IN LISTS ResolvedDependencies)
    get_filename_component(DependencyDirectory "${Dependency}" DIRECTORY)
    if(NOT DependencyDirectory STREQUAL RuntimeDirectory)
        get_filename_component(DependencyName "${Dependency}" NAME)
        file(COPY_FILE "${Dependency}" "${RuntimeDirectory}/${DependencyName}" ONLY_IF_DIFFERENT)
    endif()
endforeach()
