if (CSM_MINIMUM_DEMO)
  target_sources(${APP_NAME}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/LAppDefine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LAppDefine.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LAppPal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LAppPal.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LAppTextureManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LAppTextureManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mainMinimum.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CubismUserModelExtend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CubismUserModelExtend.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MouseActionManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MouseActionManager.hpp
  )
else ()
  target_sources(${APP_NAME}
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppConfig.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppConfig.hpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppDefine.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppDefine.hpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppDelegate.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppDelegate.hpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppPlatform.hpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppIPC.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppIPC.hpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppLive2DManager.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppLive2DManager.hpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppModel.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppModel.hpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppPal.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppPal.hpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppSprite.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppSprite.hpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppSpriteShader.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppSpriteShader.hpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppTextureManager.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppTextureManager.hpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppView.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/LAppView.hpp
      ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
  )
  if (APPLE)
    target_sources(${APP_NAME}
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/LAppMacOS.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/LAppMacOS.hpp
    )
  else ()
    target_sources(${APP_NAME}
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/LAppWayland.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/LAppWayland.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/LAppWaylandRegion.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/LAppWaylandRegion.hpp
    )
  endif ()
endif ()

target_include_directories(${APP_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
