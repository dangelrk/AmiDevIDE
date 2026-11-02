# These two suites read real NDK data out of the generated toolchain tree.
# AMIGAIDE_TOOLCHAIN_ROOT is set by the top-level project; both tests skip
# themselves when it has not been bootstrapped yet, so a fresh clone still
# gets a green ctest run.
find_package(Qt6 REQUIRED COMPONENTS Test)

function(amigaide_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE amigaide_core Qt6::Test)
    target_include_directories(${name} PRIVATE ${AMIGAIDE_ROOT}/qt)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

amigaide_add_test(tst_appsettings tst_appsettings.cpp)
amigaide_add_test(tst_diagnosticsparser tst_diagnosticsparser.cpp)

amigaide_add_test(tst_makefilegenerator tst_makefilegenerator.cpp)
target_compile_definitions(tst_makefilegenerator PRIVATE GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

amigaide_add_test(tst_iconwriter tst_iconwriter.cpp)

amigaide_add_test(tst_sfdindex tst_sfdindex.cpp)
target_compile_definitions(tst_sfdindex PRIVATE TOOLCHAIN_ROOT="${AMIGAIDE_TOOLCHAIN_ROOT}")

amigaide_add_test(tst_csymbolparser tst_csymbolparser.cpp)
amigaide_add_test(tst_asmsymbolparser tst_asmsymbolparser.cpp)
amigaide_add_test(tst_basicsymbolparser tst_basicsymbolparser.cpp)

amigaide_add_test(tst_autodocindex tst_autodocindex.cpp)
target_compile_definitions(tst_autodocindex PRIVATE TOOLCHAIN_ROOT="${AMIGAIDE_TOOLCHAIN_ROOT}")

amigaide_add_test(tst_hunkreader tst_hunkreader.cpp)
amigaide_add_test(tst_m68kdisassembler tst_m68kdisassembler.cpp)

amigaide_add_test(tst_includegraph tst_includegraph.cpp)

amigaide_add_test(tst_projectsearch tst_projectsearch.cpp)

amigaide_add_test(tst_structlayout tst_structlayout.cpp)

amigaide_add_test(tst_project tst_project.cpp)

amigaide_add_test(tst_sdkregistry tst_sdkregistry.cpp)

amigaide_add_test(tst_logtail tst_logtail.cpp)
amigaide_add_test(tst_vcs tst_vcs.cpp)
amigaide_add_test(tst_vcsrunner tst_vcsrunner.cpp)
amigaide_add_test(tst_vcsgui tst_vcsgui.cpp)
set_tests_properties(tst_vcsgui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
