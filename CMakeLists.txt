cmake_minimum_required(VERSION 3.16)
project(konqix VERSION 0.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui Sql Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PURPLE REQUIRED IMPORTED_TARGET purple)
# KF6WindowSystem is optional but strongly recommended: it bypasses KWin's
# focus-stealing-prevention delay when we raise the buddy list from the tray.
find_package(KF6WindowSystem QUIET)
# Optional Hunspell: powers the spell-check overlay on the conversation
# input. Pure C library, no KDE dependency.
pkg_check_modules(HUNSPELL IMPORTED_TARGET hunspell)

add_executable(konqix
    src/main.cpp
    src/PurpleCore.cpp
    src/QtEventLoop.cpp
    src/MainWindow.cpp
    src/BuddyListModel.cpp
    src/ConversationWindow.cpp
    src/ConversationManager.cpp
    src/AccountsDialog.cpp
    src/AccountEditDialog.cpp
    src/TrayIcon.cpp
    src/RequestDispatcher.cpp
    src/NotifyDispatcher.cpp
    src/Notifier.cpp
    src/MessageState.cpp
    src/HistoryDialog.cpp
    src/LogFormat.cpp
    src/LogIndex.cpp
    resources/icons.qrc
)

if (HUNSPELL_FOUND)
    target_sources(konqix PRIVATE src/SpellChecker.cpp)
endif()

target_link_libraries(konqix
    PRIVATE
        Qt6::Widgets
        Qt6::Core
        Qt6::Gui
        Qt6::Sql
        Qt6::Network
        PkgConfig::PURPLE
)

if (KF6WindowSystem_FOUND)
    target_link_libraries(konqix PRIVATE KF6::WindowSystem)
    target_compile_definitions(konqix PRIVATE HAVE_KWINDOWSYSTEM)
    message(STATUS "KF6::WindowSystem detected — using activation token API")
else()
    message(STATUS "KF6WindowSystem NOT found — tray activation may be slow")
endif()

if (HUNSPELL_FOUND)
    target_link_libraries(konqix PRIVATE PkgConfig::HUNSPELL)
    target_compile_definitions(konqix PRIVATE HAVE_HUNSPELL)
    message(STATUS "Hunspell detected — spell-check on input enabled")
else()
    message(STATUS "Hunspell NOT found — spell-check on conversation input disabled")
endif()

target_include_directories(konqix PRIVATE src)

include(GNUInstallDirs)

install(TARGETS konqix
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES packaging/com.konqix.Konqix.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# The .desktop filename is intentionally reverse-DNS so its basename
# does NOT match our window's WM_CLASS=konqix; KDE Plasma matches by
# both basename and StartupWMClass, so this combined with the
# StartupWMClass= line in the .desktop keeps the running window's title
# bar under our per-status setWindowIcon() control while the launcher
# still picks up the Icon=konqix below.
install(FILES resources/konqix.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
