# Prefer the package target: recent RE2 versions carry transitive Abseil dependencies.
add_library (amule_regex_engine INTERFACE)
find_package (re2 CONFIG QUIET)
if (TARGET re2::re2)
 target_link_libraries (amule_regex_engine INTERFACE re2::re2)
endif ()
if (NOT TARGET re2::re2)
 find_package (PkgConfig REQUIRED)
 pkg_check_modules (RE2 REQUIRED IMPORTED_TARGET re2)
 if (CMAKE_EXE_LINKER_FLAGS MATCHES "(^| )-static($| )")
  # pkg-config's ordinary result omits private static dependencies (Abseil
  # on newer RE2). Preserve the complete link closure for portable builds.
  execute_process (COMMAND ${PKG_CONFIG_EXECUTABLE} --libs --static re2
   OUTPUT_VARIABLE RE2_STATIC_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE
   RESULT_VARIABLE RE2_STATIC_RESULT)
  if (NOT RE2_STATIC_RESULT EQUAL 0)
   message (FATAL_ERROR "Could not determine RE2 static dependencies")
  endif ()
  separate_arguments (RE2_STATIC_FLAGS NATIVE_COMMAND "${RE2_STATIC_FLAGS}")
  set_property (TARGET PkgConfig::RE2 APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${RE2_STATIC_FLAGS})
 endif ()
 target_link_libraries (amule_regex_engine INTERFACE PkgConfig::RE2)
endif ()

add_library (mulecommon STATIC
	FileFunctions.cpp
	Format.cpp
	LocaleInit.cpp
	MD5Sum.cpp
	MediaCodecName.cpp
	MuleDebug.cpp
	Path.cpp
	PasswordInput.cpp
	RateLimiter.cpp
	Regex.cpp
	strerror_r.c
	StringFunctions.cpp
	TextFile.cpp
)

target_include_directories (mulecommon
	PRIVATE ${amule_BINARY_DIR}
)

target_link_libraries (mulecommon
	PUBLIC wxWidgets::BASE
 PRIVATE amule_regex_engine
)

if (HAVE_BFD)
	target_link_libraries (mulecommon
		PUBLIC ${BFD_LIBRARY}
	)
endif()
