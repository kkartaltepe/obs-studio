include(FetchContent)

FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG v0.4 # Optionally specify a commit hash, version tag or branch here
)
FetchContent_MakeAvailable(Corrosion)

# Import targets defined in a package or workspace manifest `Cargo.toml` file
corrosion_import_crate(MANIFEST_PATH Cargo.toml)

#target crasher
target_include_directories(crasher INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
add_library(OBS::crasher ALIAS crasher)

# install the underlying target.
get_target_property(crasher-lib crasher INTERFACE_LINK_LIBRARIES)
setup_binary_target(${crasher-lib})
