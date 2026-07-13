cmake_minimum_required(VERSION 3.10)
# set sln name
set(TARGET_NAME bytetrack_psin)
option(LINUX_AARCH64 "option for arrch64 arch on" OFF)
message("LINUX_AARCH64 Value: ${LINUX_AARCH64}")
project(${TARGET_NAME} LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
set(CMAKE_COLOR_DIAGNOSTICS TRUE)

set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR})
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>")
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
#/Zi 是 MSVC 编译器的一个选项，用于生成调试信息
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/Zi>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/Zi>")
#/permissive- 用于启用更严格的语法检查和符合性检查
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/permissive->")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/permissive->")
#/wd4244 是 MSVC 编译器的一个选项，用于禁止特定的警告
#add_compile_options("$<$<C_COMPILER_ID:MSVC>:/wd4244>")
#add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/wd4244>")
#/INCREMENTAL:NO 是 MSVC 链接器的一个选项，用于禁用增量链接
add_link_options("$<$<C_COMPILER_ID:MSVC>:/INCREMENTAL:NO>")
add_link_options("$<$<CXX_COMPILER_ID:MSVC>:/INCREMENTAL:NO>")
add_link_options("$<$<C_COMPILER_ID:MSVC>:/DEBUG>")
add_link_options("$<$<CXX_COMPILER_ID:MSVC>:/DEBUG>")
#/OPT:REF 是 MSVC 链接器的一个选项，用于进行引用消除优化
add_link_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/OPT:REF>")
add_link_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/OPT:REF>")
#/OPT:ICF 是 MSVC 链接器的一个选项，用于进行函数节省优化（Identical Code Folding）
add_link_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/OPT:ICF>")
add_link_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/OPT:ICF>")

include_directories(
	./modelzoo_utils
	src/bt_include/
	)
aux_source_directory(src/bt_src/. BT_SRC)

if (WIN32)
    # Windows 操作系统
    set_property(GLOBAL PROPERTY USE_FOLDERS OFF)
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${TARGET_NAME}) # 设置目标项目作为启动项目
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD /Zi")
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
    add_definitions(-DYAML_CPP_STATIC_DEFINE)

	include_directories(
	C:/IcraftMdz/ThirdParty_v0.2/Eigen-3.4.0/include
	C:/IcraftMdz/ThirdParty_v0.2/YamlCpp/include
	C:/IcraftMdz/ThirdParty_v0.2/opencv2/include
	)
	link_directories(C:/IcraftMdz/ThirdParty_v0.2/YamlCpp/lib)
	link_directories(C:/IcraftMdz/ThirdParty_v0.2/opencv2/lib)
	link_libraries(
	opencv_world455 opencv_img_hash455 ade IlmImf ippicvmt ippiw
	ittnotify libjpeg-turbo libopenjp2 libpng libprotobuf libtiff libwebp quirc zlib
    yaml-cpp
    )

	find_package(Icraft-BuyiBackend REQUIRED)
	find_package(Icraft-HostBackend REQUIRED)
	find_package(icraft-zg330backend REQUIRED)

	add_executable(${TARGET_NAME} src/${TARGET_NAME}.cpp ${BT_SRC})
	target_link_libraries(${TARGET_NAME} PRIVATE Icraft::HostBackend Icraft::BuyiBackend Icraft::ZG330Backend)

elseif(UNIX)
    # Unix/Linux 操作系统
	include_directories(/ModelzooDeps/include/Eigen-3.4.0)
	if (LINUX_AARCH64)
		set(CMAKE_SYSTEM_NAME Linux)
		set(CMAKE_SYSTEM_PROCESSOR aarch64)
		SET(CMAKE_C_COMPILER /usr/bin/aarch64-linux-gnu-gcc)
		SET(CMAKE_CXX_COMPILER /usr/bin/aarch64-linux-gnu-g++)

		list(APPEND CMAKE_PREFIX_PATH /ModelzooDeps/aarch64/Static/lib/cmake/opencv4)
		list(APPEND CMAKE_PREFIX_PATH /ModelzooDeps/aarch64/Static/lib/cmake/yaml-cpp)
		link_directories(/ModelzooDeps/aarch64/Static/lib/)

		find_package(OpenCV REQUIRED)
		find_package(yaml-cpp REQUIRED)
	
		include_directories(${OpenCV_INCLUDE_DIRS})
		include_directories(${YAML_CPP_INCLUDE_DIR})
		link_libraries(yaml-cpp)
		find_package(Icraft-BuyiBackend REQUIRED)
		find_package(Icraft-HostBackend REQUIRED)
		find_package(icraft-zg330backend REQUIRED)
		add_executable(${TARGET_NAME} src/${TARGET_NAME}.cpp ${BT_SRC})
		target_link_libraries(${TARGET_NAME} PRIVATE Icraft::HostBackend Icraft::BuyiBackend Icraft::ZG330Backend ${OpenCV_LIBS} libyaml-cpp.a)

	else()
		list(APPEND CMAKE_PREFIX_PATH /ModelzooDeps/x64/Dynamic//lib/cmake/opencv4)
		list(APPEND CMAKE_PREFIX_PATH /ModelzooDeps/x64/Dynamic//lib/cmake/yaml-cpp)
		link_directories(/ModelzooDeps/x64/Dynamic/lib/)
		find_package(OpenCV REQUIRED)
		find_package(yaml-cpp REQUIRED)

		include_directories(${OpenCV_INCLUDE_DIRS})
		include_directories(${YAML_CPP_INCLUDE_DIR})

		link_libraries(yaml-cpp)
		find_package(Icraft-BuyiBackend REQUIRED)
		find_package(Icraft-HostBackend REQUIRED)
		find_package(icraft-zg330backend REQUIRED)
		add_executable(${TARGET_NAME} src/${TARGET_NAME}.cpp ${BT_SRC})
		target_link_libraries(${TARGET_NAME} PRIVATE Icraft::HostBackend Icraft::BuyiBackend Icraft::ZG330Backend ${OpenCV_LIBS})
	endif()
endif()


# cmake ..
# cmake --build . --config Release
# cmake ..
# make -j
# cmake -DLINUX_AARCH64=ON ..
# make -j