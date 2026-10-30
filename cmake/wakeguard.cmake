# Sleep inhibition is shared by the core executables, never the remote GUI.
# The pinned MIT dependency is vendored so distro, AppImage and Flatpak builds
# need neither a new system package nor a configure-time network connection.
option (ENABLE_SLEEP_INHIBITION "Enable cross-platform sleep inhibition" ON)
set (AMULE_SLEEP_SUPPORTED FALSE)
if (ENABLE_SLEEP_INHIBITION AND (BUILD_MONOLITHIC OR BUILD_DAEMON))
	function (amule_add_wakeguard)
		# Keep this dependency static without changing aMule's other libraries.
		set (BUILD_SHARED_LIBS OFF)
		# This partial upstream snapshot intentionally omits tests/examples.
		set (WAKEGUARD_BUILD_TESTS OFF CACHE INTERNAL "Test standalone libwakeguard separately" FORCE)
		set (WAKEGUARD_BUILD_EXAMPLES OFF CACHE INTERNAL "Do not build vendored examples" FORCE)
		set (WAKEGUARD_REQUIRE_DBUS_TESTS OFF CACHE INTERNAL "No vendored test fixtures" FORCE)
		if (UNIX AND NOT APPLE)
			find_package (PkgConfig QUIET)
			if (PkgConfig_FOUND)
				pkg_check_modules (AMULE_WAKEGUARD_GIO QUIET gio-unix-2.0>=2.40)
			endif()
			set (WAKEGUARD_WITH_DBUS ${AMULE_WAKEGUARD_GIO_FOUND} CACHE BOOL
				"Build libwakeguard Unix D-Bus backends")
		endif()
		add_subdirectory (src/extern/libwakeguard EXCLUDE_FROM_ALL)
	endfunction()
	amule_add_wakeguard()
	get_target_property (AMULE_SLEEP_SUPPORTED wakeguard WAKEGUARD_HAS_BACKEND)
endif()

if (TARGET wakeguard::wakeguard)
	install (FILES
		${CMAKE_SOURCE_DIR}/src/extern/libwakeguard/LICENSE
		${CMAKE_SOURCE_DIR}/src/extern/libwakeguard/NOTICE.md
		DESTINATION ${CMAKE_INSTALL_DOCDIR}/libwakeguard)
	install (FILES ${CMAKE_SOURCE_DIR}/src/extern/libwakeguard/third_party/keepawake-rs.LICENSE
		DESTINATION ${CMAKE_INSTALL_DOCDIR}/libwakeguard/third_party)
endif()
