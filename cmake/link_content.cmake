# NMK_CONTENT_LINK (CMakeLists.txt): make DST (<build>/content) a directory junction to SRC (the source content/), so a
# build folder holds the exe and not another 5 GB copy. Run as  cmake -DSRC=... -DDST=... -P link_content.cmake.
# Never deletes through a link: an existing junction / symlink is only unlinked (rmdir), a real directory (an earlier
# copy) is removed as itself. Windows: mklink /J (no admin rights needed); elsewhere a symbolic link.
file(TO_NATIVE_PATH "${SRC}" srcN)
file(TO_NATIVE_PATH "${DST}" dstN)
get_filename_component(parent "${DST}" DIRECTORY)
get_filename_component(name "${DST}" NAME)

set(isLink FALSE)
if(IS_SYMLINK "${DST}")
  set(isLink TRUE)
elseif(WIN32 AND EXISTS "${DST}")   # a junction that IS_SYMLINK does not report: dir /AL lists reparse points
  file(TO_NATIVE_PATH "${parent}" parentN)
  execute_process(COMMAND cmd /c dir /AL "${parentN}" OUTPUT_VARIABLE listing ERROR_QUIET)
  string(REGEX MATCH "<(JUNCTION|SYMLINKD)>[ \t]+${name} \\[([^]\r\n]*)\\]" m "${listing}")
  if(m)
    set(isLink TRUE)
  endif()
endif()

if(isLink)
  file(REAL_PATH "${DST}" cur)
  file(REAL_PATH "${SRC}" want)
  if(cur STREQUAL want)
    return()   # already linked here
  endif()
  if(WIN32)
    execute_process(COMMAND cmd /c rmdir "${dstN}" RESULT_VARIABLE rc)   # removes the junction itself, not its target
  else()
    file(REMOVE "${DST}")
  endif()
elseif(EXISTS "${DST}")
  message(STATUS "content: replacing the copied ${DST} with a link to ${SRC}")
  file(REMOVE_RECURSE "${DST}")   # a real directory (no link was found above)
endif()

if(WIN32)
  execute_process(COMMAND cmd /c mklink /J "${dstN}" "${srcN}" RESULT_VARIABLE rc OUTPUT_QUIET)
else()
  file(CREATE_LINK "${SRC}" "${DST}" SYMBOLIC RESULT rc)
endif()
if(NOT rc EQUAL 0 OR NOT EXISTS "${DST}/settings")
  message(FATAL_ERROR "content: could not link ${DST} -> ${SRC} (${rc}); configure with -DNMK_CONTENT_LINK=OFF to copy instead")
endif()
