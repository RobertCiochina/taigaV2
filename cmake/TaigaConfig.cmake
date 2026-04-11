add_library(taiga-config INTERFACE)

target_compile_definitions(taiga-config INTERFACE
	QT_DISABLE_DEPRECATED_UP_TO=0x060A00
)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
	target_compile_definitions(taiga-config INTERFACE
		_WIN32_WINNT=0x0A00
		WIN32_LEAN_AND_MEAN
		NOMINMAX
		_UNICODE
		UNICODE
	)
endif()

if (MSVC)
	target_compile_options(taiga-config INTERFACE
		/guard:cf
		/MP
		/permissive-
		/utf-8
		/W3
		/Zc:__cplusplus
		# Line-number PDBs for RelWithDebInfo so OpenCppCoverage (and debuggers) see source mapping.
		"$<$<CONFIG:RelWithDebInfo>:/Zi>"
	)
	target_link_options(taiga-config INTERFACE "$<$<CONFIG:RelWithDebInfo>:/DEBUG>")
else()
	target_compile_options(taiga-config INTERFACE
		-Wall
		-Wextra
	)
endif()

# Code coverage (GCC/Clang). MSVC users typically rely on OpenCppCoverage or a Clang build.
option(TAIGA_ENABLE_COVERAGE "Instrument targets with coverage flags (non-MSVC only)" OFF)
if(TAIGA_ENABLE_COVERAGE)
	if(MSVC)
		message(WARNING "TAIGA_ENABLE_COVERAGE is not applied for MSVC; use a MinGW/Clang build or an external coverage tool.")
	else()
		target_compile_options(taiga-config INTERFACE --coverage -O0 -g)
		target_link_options(taiga-config INTERFACE --coverage)
	endif()
endif()
