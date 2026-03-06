add_subdirectory(Core)
add_subdirectory(Network)

add_executable(mcc main.cpp)

target_link_libraries(mcc
    PRIVATE
        Core
        Network
)
