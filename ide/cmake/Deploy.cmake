# Runs macdeployqt as a POST_BUILD step on `target` so every normal build
# produces a self-contained .app: Qt's frameworks get copied into
# Contents/Frameworks and the binary's load commands rewritten to
# @rpath-relative paths. Without this, the binary keeps the absolute
# Homebrew path (e.g. /opt/homebrew/opt/qt/...) it was linked against, which
# only resolves on the machine that path belongs to -- copying the bundle to
# another Mac then fails at launch with a DYLD "Library not loaded" abort.
# (windeployqt is the documented equivalent step on Windows -- run it
# manually via the Qt install's bin/ directory, e.g.
#   windeployqt build\src\amigaide.exe
# since CMake can't locate it portably without qt-cmake's own tooling.)
function(amigaide_add_deploy_target target)
  if(APPLE)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${QT6_INSTALL_PREFIX}/bin" "/opt/homebrew/opt/qt/bin")
    if(MACDEPLOYQT_EXECUTABLE)
      # macdeployqt rewrites binaries, which breaks the linker's ad-hoc
      # signature ("code has no resources but signature indicates they
      # must be present"). On Apple Silicon the kernel then kills the
      # program at launch with SIGKILL and no message -- so sign the
      # bundle again, ad hoc, right after.
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${MACDEPLOYQT_EXECUTABLE} $<TARGET_BUNDLE_DIR:${target}>
        COMMAND codesign --force --deep --sign - $<TARGET_BUNDLE_DIR:${target}>
        COMMENT "Bundling Qt frameworks into $<TARGET_BUNDLE_DIR:${target}> via macdeployqt, signing ad hoc"
        VERBATIM
      )
    else()
      message(WARNING "macdeployqt not found -- ${target}.app will only run on machines with Qt installed at the same Homebrew path it was built against. Install Qt via 'brew install qt' and reconfigure to fix.")
    endif()
  endif()
endfunction()
