# Installation layout and CPack packaging.
#
# Windows: an NSIS installer (Pullform-<version>-win64.exe) with Start menu and
# optional desktop shortcuts and an uninstaller. The executable links the C/C++
# runtime statically and embeds its fonts and icon, so the installer ships
# everything it needs; OpenGL comes from the graphics driver.
# Linux/macOS: a .tar.gz of the same layout (AppImage/.app bundles are roadmap items).

include(GNUInstallDirs)

if(WIN32)
  install(TARGETS pullform RUNTIME DESTINATION .)
  set(PULLFORM_DOC_DIR .)
else()
  install(TARGETS pullform RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  set(PULLFORM_DOC_DIR ${CMAKE_INSTALL_DOCDIR})
endif()
install(FILES
  ${PROJECT_SOURCE_DIR}/LICENSE
  ${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
  ${PROJECT_SOURCE_DIR}/README.md
  DESTINATION ${PULLFORM_DOC_DIR})

set(CPACK_PACKAGE_NAME "Pullform")
set(CPACK_PACKAGE_VENDOR "Pullform contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Pullform")
set(CPACK_PACKAGE_EXECUTABLES "Pullform" "Pullform")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME "Pullform-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_STRIP_FILES ON)

if(WIN32)
  set(CPACK_GENERATOR "NSIS")
  set(CPACK_PACKAGE_FILE_NAME "Pullform-${PROJECT_VERSION}-win64")
  set(CPACK_NSIS_PACKAGE_NAME "Pullform ${PROJECT_VERSION}")
  set(CPACK_NSIS_DISPLAY_NAME "Pullform")
  set(CPACK_NSIS_INSTALLED_ICON_NAME "Pullform.exe")
  set(CPACK_NSIS_MUI_ICON "${PROJECT_SOURCE_DIR}/assets/icons/pullform.ico")
  set(CPACK_NSIS_MUI_UNIICON "${PROJECT_SOURCE_DIR}/assets/icons/pullform.ico")
  set(CPACK_NSIS_URL_INFO_ABOUT "${PROJECT_HOMEPAGE_URL}")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_NSIS_MODIFY_PATH OFF)
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
  set(CPACK_NSIS_MUI_FINISHPAGE_RUN "Pullform.exe")
  set(CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortCut '$DESKTOP\\\\Pullform.lnk' '$INSTDIR\\\\Pullform.exe'")
  set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete '$DESKTOP\\\\Pullform.lnk'")
  set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
else()
  set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
