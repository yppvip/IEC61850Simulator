if(NOT DEFINED SourcePath OR NOT DEFINED DestinationPath)
    message(FATAL_ERROR "SourcePath and DestinationPath are required.")
endif()

if(NOT EXISTS "${SourcePath}")
    message(FATAL_ERROR "Default runtime configuration does not exist: ${SourcePath}")
endif()

if(NOT EXISTS "${DestinationPath}")
    get_filename_component(DestinationDirectory "${DestinationPath}" DIRECTORY)
    file(MAKE_DIRECTORY "${DestinationDirectory}")
    file(COPY_FILE "${SourcePath}" "${DestinationPath}")
endif()
