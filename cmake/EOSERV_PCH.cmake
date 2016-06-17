# $Id$
# Copyright (c) Julian Smythe, All rights reserved.
#
# This software is provided 'as-is', without any express or implied
# warranty.  In no event will the authors be held liable for any damages
# arising from the use of this software.
#
# Permission is granted to anyone to use this software for any purpose,
# including commercial applications, and to alter it and redistribute it
# freely, subject to the following restrictions:
#
# 1. The origin of this software must not be misrepresented; you must not
#    claim that you wrote the original software. If you use this software
#    in a product, an acknowledgment in the product documentation would be
#    appreciated but is not required.
#
# 2. Altered source versions must be plainly marked as such, and must not be
#    misrepresented as being the original software.
#
# 3. This notice may not be removed or altered from any source distribution.
#

macro(compile_pch TargetName HeaderFile)
	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang"
	OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		set(_PCH_GCC TRUE)
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
		set(_PCH_MSVC TRUE)
	else()
		message(WARNING "Sorry, we don't support precompiled headers for your compiler. Detected compiler was ${CMAKE_CXX_COMPILER_ID}.")
		return()
	endif()

	# Not tested at all
	if(_PCH_MSVC)
		message(WARNING "Precompiled headers are untested on MSVC. They may not work at all.")
	endif()

	get_directory_property(_PCHDefinitions COMPILE_DEFINITIONS)
	get_directory_property(_PCHIncludes INCLUDE_DIRECTORIES)

	foreach(Def ${_PCHDefinitions})
		if(_PCH_GCC)
			list(APPEND _PCHFlags "-D${Def}")
		elseif(_PCH_MSVC)
			list(APPEND _PCHFlags "/D${Def}")
		endif()
	endforeach()

	set(_PCHFlags "${_PCHFlags} ${CMAKE_CXX_FLAGS}")

	string(TOLOWER "${CMAKE_BUILD_TYPE}" _PCHBuildType)

	if("${_PCHBuildType}" STREQUAL "debug")
		set(_PCHFlags "${_PCHFlags} ${CMAKE_CXX_FLAGS_DEBUG}")
	elseif("${_PCHBuildType}" STREQUAL "minsizerel")
		set(_PCHFlags "${_PCHFlags} ${CMAKE_CXX_FLAGS_MINSIZEREL}")
	elseif("${_PCHBuildType}" STREQUAL "release")
		set(_PCHFlags "${_PCHFlags} ${CMAKE_CXX_FLAGS_RELEASE}")
	elseif("${_PCHBuildType}" STREQUAL "relwithdebinfo")
		set(_PCHFlags "${_PCHFlags} ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
	endif()

	foreach(Inc ${_PCHIncludes})
		if(_PCH_GCC)
			list(APPEND _PCHFlags "-I${Inc}")
		elseif(_PCH_MSVC)
			list(APPEND _PCHFlags "/I${Inc}")
		endif()
	endforeach()

	SEPARATE_ARGUMENTS(_PCHFlags)

	if(_PCH_GCC)
		add_custom_command(OUTPUT ${HeaderFile}.gch
			COMMAND ${CMAKE_CXX_COMPILER} ${_PCHFlags} -x c++-header ${HeaderFile} -o ${HeaderFile}.gch
			MAIN_DEPENDENCY ${HeaderFile}
			DEPENDS ${HeaderFile}
		)

		add_custom_target(${TargetName}
			DEPENDS ${HeaderFile}.gch
		)

		set("${TargetName}_INCLUDE_FLAG" "-include \"${HeaderFile}\"")
	elseif(_PCH_MSVC)

		if(EXISTS "${CMAKE_MODULE_PATH}/EOSERV_PCH_MSVC.cpp.in")
			set(InFile "${CMAKE_MODULE_PATH}/EOSERV_PCH_MSVC.cpp.in")
		elseif(EXISTS "${CMAKE_ROOT}/EOSERV_PCH_MSVC.cpp.in")
			set(InFile "${CMAKE_ROOT}/EOSERV_PCH_MSVC.cpp.in")
		else()
			message(WARNING "Could not locate EOSERV_PCH_MSVC.cpp")
			return()
		endif()

		set(_PCH_AUTOGEN "This file was automatically generated by EOSERV_PCH.cmake")
		set(_PCH_HEADER_FILE "${HeaderFile}")
		configure_file(${InFile} ${HeaderFile}.cpp)

		add_custom_command(OUTPUT ${HeaderFile}.pch
			COMMAND ${CMAKE_CXX_COMPILER} ${_PCHFlags} /Tp\"${HeaderFile}.cpp\" /Fp\"${HeaderFile}.pch\" /Tu\"${HeaderFile}\"
			MAIN_DEPENDENCY ${HeaderFile}.cpp
			DEPENDS ${HeaderFile} ${HeaderFile}.cpp
		)

		add_custom_target(${TargetName}
			DEPENDS ${HeaderFile}.pch
		)

		set("${TargetName}_INCLUDE_FLAG" "/Yu\"${HeaderFile}\"")
	endif()
endmacro()