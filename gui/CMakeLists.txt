# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

add_subdirectory(gui)
add_subdirectory(ged)
add_subdirectory(fitpanel)
add_subdirectory(guibuilder)
add_subdirectory(guihtml)
add_subdirectory(recorder)
add_subdirectory(sessionviewer)

if(webui)
if (NOT FIXME_TEMPORARILY_EXCLUDED_FOR_RUNTIME_CXXMODULES)
   add_subdirectory(webdisplay)
   if(cefweb)
      add_subdirectory(cefdisplay)
   endif()
   if(qt5web)
      add_subdirectory(qt5webdisplay)
   endif()
   add_subdirectory(webgui6)

   if(root7)
      add_subdirectory(browserv7)
      add_subdirectory(canvaspainter)
      add_subdirectory(fitpanelv7)
   endif()
endif()
endif()

