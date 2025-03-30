cmake_minimum_required(VERSION 3.14)
project(mp3_organizer)

# Настройка стандарта C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Поиск FUSE3 (без pkg-config)
find_library(FUSE3_LIBRARY
        NAMES fuse3 fuse
        PATHS /usr/lib /usr/local/lib /opt/local/lib
)

find_path(FUSE3_INCLUDE_DIR
        NAMES fuse3/fuse.h fuse/fuse.h
        PATHS /usr/include /usr/local/include /opt/local/include
)

if(NOT FUSE3_LIBRARY OR NOT FUSE3_INCLUDE_DIR)
    message(FATAL_ERROR "FUSE3 not found! Install with:\n"
            "  Ubuntu/Debian: sudo apt-get install libfuse3-dev\n"
            "  Fedora: sudo dnf install fuse3-devel")
endif()

# 2. Поиск TagLib (без pkg-config)
find_library(TAGLIB_LIBRARY
        NAMES tag
        PATHS /usr/lib /usr/local/lib /opt/local/lib
)

find_path(TAGLIB_INCLUDE_DIR
        NAMES tag.h taglib/tag.h
        PATHS /usr/include /usr/local/include /opt/local/include
)

if(NOT TAGLIB_LIBRARY OR NOT TAGLIB_INCLUDE_DIR)
    # Альтернатива: скачиваем TagLib через FetchContent
    include(FetchContent)

    FetchContent_Declare(
            taglib
            GIT_REPOSITORY https://github.com/taglib/taglib.git
            GIT_TAG v1.12
    )
    FetchContent_MakeAvailable(taglib)

    set(TAGLIB_FOUND TRUE)
else()
    # Создаем импортированную цель для системного TagLib
    add_library(TagLib::TagLib UNKNOWN IMPORTED)
    set_target_properties(TagLib::TagLib PROPERTIES
            IMPORTED_LOCATION "${TAGLIB_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${TAGLIB_INCLUDE_DIR}"
    )
    set(TAGLIB_FOUND TRUE)
endif()

# 3. Создание исполняемого файла
add_executable(mp3_organizer main.cpp)

# 4. Подключение зависимостей
if(TAGLIB_FOUND AND TARGET taglib)
    target_link_libraries(mp3_organizer PRIVATE taglib)
elseif(TAGLIB_FOUND AND TARGET TagLib::TagLib)
    target_link_libraries(mp3_organizer PRIVATE TagLib::TagLib)
else()
    message(FATAL_ERROR "TagLib not found and could not be downloaded!")
endif()

target_link_libraries(mp3_organizer PRIVATE ${FUSE3_LIBRARY})
target_include_directories(mp3_organizer PRIVATE ${FUSE3_INCLUDE_DIR})

# 5. Для старых версий GCC
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(mp3_organizer PRIVATE stdc++fs)
endif()

# 6. Вывод информации о найденных библиотеках
message(STATUS "FUSE3 found: ${FUSE3_LIBRARY} (includes: ${FUSE3_INCLUDE_DIR})")
if(TARGET taglib)
    message(STATUS "Using TagLib from FetchContent")
else()
    message(STATUS "Using system TagLib: ${TAGLIB_LIBRARY}")
endif()