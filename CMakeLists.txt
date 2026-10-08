cmake_minimum_required (VERSION 3.31.5)
project(SimpleSwitcher VERSION 1.0 LANGUAGES C CXX)

# Глобальные настройки

if(WIN7_COMPAT)
	add_compile_definitions(_WIN32_WINNT=0x0601)
	add_compile_definitions(SS_WIN_7_COMPAT)
else()
endif()

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
#set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
	add_compile_options(/utf-8)
	add_compile_options(/std:c++latest)
	add_compile_options(/we5030)
	add_compile_options(/GF) # String Pooling

    # 1) Dead Code Elimination (Удаление мертвого кода)
    add_compile_options($<$<CONFIG:Release>:/Gy>)     # /Gy — разделяет функции на сегменты, чтобы линковщик мог их удалить
    add_link_options($<$<CONFIG:Release>:/OPT:REF>)     # /OPT:REF — удаляет функции, на которые нет ссылок


endif()

# Проверка и включение IPO только после объявления project
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
	message("IPO ENABLED")
else()
    message(WARNING "IPO is NOT supported: ${ipo_error}")
endif()


# Версия FluentSwitcher - одна на программу, свойства файла и окно настроек: build/.../generated/fs_version.h.
set(FLUENTSWITCHER_VERSION 1.5.0)
# Бета: "-beta1" - в названии версии ("1.5.0-beta1": программа, файлы выпуска, метка v1.5.0-beta1 - выпуск на GitHub
# станет предварительным), числа свойств файла - без него. Выпуск - пусто.
set(FLUENTSWITCHER_VERSION_SUFFIX "-beta21")
string(REPLACE "." ";" fs_version_parts ${FLUENTSWITCHER_VERSION})
list(GET fs_version_parts 0 FLUENTSWITCHER_VERSION_MAJOR)
list(GET fs_version_parts 1 FLUENTSWITCHER_VERSION_MINOR)
list(GET fs_version_parts 2 FLUENTSWITCHER_VERSION_PATCH)
configure_file(src/fs_version.h.in "${CMAKE_BINARY_DIR}/generated/fs_version.h" @ONLY)

# Project
add_executable (${PROJECT_NAME} WIN32)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "FluentSwitcher")
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_sources(${PROJECT_NAME}  PRIVATE src/manifest.manifest src/res/res.rc)
target_include_directories(${PROJECT_NAME} PRIVATE "src/libtools")
target_precompile_headers(${PROJECT_NAME} PRIVATE src/precompl.h)
#target_sources(${PROJECT_NAME} PRIVATE FILE_SET CXX_MODULE_HEADER_UNITS  FILES src/precompl.h)
block()
	file(GLOB_RECURSE src  "src/*.cpp" "src/*.h" ) # CONFIGURE_DEPENDS - искать новые файлы при билде.
	list(FILTER src EXCLUDE REGEX ".*/old/.*")
	target_sources(${PROJECT_NAME} PRIVATE ${src})
	#file(GLOB_RECURSE src_modules   "src/*.cppm" "src/*.ixx" )
	#if(src_modules)
	#	 target_sources(${PROJECT_NAME} PRIVATE FILE_SET CXX_MODULES FILES ${src_modules})
	#	 set_source_files_properties(${src_modules} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
	#endif()
	target_include_directories(${PROJECT_NAME} PRIVATE "src") 
endblock()


# SHA DEFINE
block()
	# 1. Берем хэш из окружения GitHub (GITHUB_SHA есть в рантайме экшена по умолчанию)
	set(TMP_GIT_HASH $ENV{GITHUB_SHA})
	# 2. Если пусто (локальная сборка), ставим заглушку "unknown" или пустую строку ""
	if(NOT TMP_GIT_HASH)
		set(TMP_GIT_HASH "")
	endif()
	# 3. Пробрасываем в C++ (CMake сам добавит кавычки, если значение внутри кавычек в set)
	target_compile_definitions(${PROJECT_NAME} PRIVATE GIT_COMMIT_HASH="${TMP_GIT_HASH}")
endblock()


# ExternLib
block()
	file(GLOB_RECURSE src  extern/*.cpp )
	if(src)
		add_library(ExternLib OBJECT)
		target_sources(ExternLib PRIVATE ${src})
		target_link_libraries(${PROJECT_NAME} PRIVATE ExternLib)
	endif()
	target_include_directories(${PROJECT_NAME} PRIVATE "extern")
endblock()

# LibUnits
block()
	file(GLOB_RECURSE src1  "lib_units/*.cpp" )
	file(GLOB_RECURSE headers  "lib_units/*.h" )
	file(GLOB_RECURSE src2   "lib_units/*.cppm" )
	if(src1 OR src2)
		add_library(LibUnits OBJECT)
		target_sources(LibUnits PRIVATE ${src1})
		target_sources(LibUnits PRIVATE  FILE_SET CXX_MODULES FILES ${src2})
	else()
		add_library(LibUnits INTERFACE)
	endif()
	target_sources(LibUnits INTERFACE ${headers})
	target_include_directories(LibUnits INTERFACE "lib_units")
	target_link_libraries(${PROJECT_NAME} PRIVATE LibUnits)
endblock()

# Окно настроек: settings/ (wxWidgets и набор элементов FluentClipper) - в том же exe, отдельным процессом:
# FluentSwitcher.exe --settings. wxWidgets 3.3 - из FLUENTSWITCHER_WX_DIR (исходники), иначе скачивается.
block()
	set(FLUENTSWITCHER_WX_DIR "" CACHE PATH "wxWidgets 3.3 source tree; empty - download the release")
	set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
	set(wxBUILD_USE_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
	set(wxBUILD_SAMPLES OFF CACHE STRING "" FORCE)
	set(wxBUILD_TESTS OFF CACHE STRING "" FORCE)
	set(wxBUILD_DEMOS OFF CACHE BOOL "" FORCE)
	set(wxBUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
	set(wxBUILD_INSTALL OFF CACHE BOOL "" FORCE)
	set(wxBUILD_CXX_STANDARD 17 CACHE STRING "" FORCE)
	foreach(opt wxUSE_WEBVIEW wxUSE_MEDIACTRL wxUSE_STC wxUSE_RICHTEXT wxUSE_AUI wxUSE_PROPGRID wxUSE_RIBBON wxUSE_XRC
			wxUSE_HTML wxUSE_WXHTML_HELP wxUSE_MS_HTML_HELP wxUSE_HELP wxUSE_OPENGL wxUSE_WEBREQUEST wxUSE_FS_INET wxUSE_URL wxUSE_PROTOCOL)
		set(${opt} OFF CACHE BOOL "" FORCE)
	endforeach()
	foreach(opt wxUSE_LIBJPEG wxUSE_LIBTIFF wxUSE_LIBWEBP wxUSE_LUNASVG)
		set(${opt} OFF CACHE STRING "" FORCE)
	endforeach()
	# Свои флаги проекта (C++ latest, /we5030, сквозная оптимизация) wxWidgets не нужны.
	get_directory_property(own_options COMPILE_OPTIONS)
	set_directory_properties(PROPERTIES COMPILE_OPTIONS "")
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE)
	if(FLUENTSWITCHER_WX_DIR)
		add_subdirectory("${FLUENTSWITCHER_WX_DIR}" "${CMAKE_BINARY_DIR}/wx" EXCLUDE_FROM_ALL)
	else()
		include(FetchContent)
		FetchContent_Declare(wxWidgets
			URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.3/wxWidgets-3.3.3.tar.bz2
			EXCLUDE_FROM_ALL)
		FetchContent_MakeAvailable(wxWidgets)
	endif()
	set_directory_properties(PROPERTIES COMPILE_OPTIONS "${own_options}")

	file(GLOB src settings/src/*.cpp settings/fluentui/*.cpp)
	add_library(SettingsUi STATIC ${src})
	target_include_directories(SettingsUi PRIVATE settings/fluentui extern "${CMAKE_BINARY_DIR}/generated")
	target_link_libraries(SettingsUi PUBLIC wx::core wx::base dwmapi)
	target_link_libraries(${PROJECT_NAME} PRIVATE SettingsUi imm32 dwmapi)
	# The word lists in the resources (src/res/res.rc): a new list - the program is rebuilt.
	set_property(SOURCE src/res/res.rc APPEND PROPERTY OBJECT_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/src/data/words_ru.txt" "${CMAKE_CURRENT_SOURCE_DIR}/src/data/words_en.txt"
		"${CMAKE_CURRENT_SOURCE_DIR}/src/data/words_ru_common.txt" "${CMAKE_CURRENT_SOURCE_DIR}/src/data/words_en_common.txt")
endblock()
