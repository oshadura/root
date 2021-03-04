// @(#)root/io:$Id$
// Author: Jakob Blomer

/*************************************************************************
 * Copyright (C) 1995-2018, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include <ROOT/RConfig.h>
#include <ROOT/RRawFile.hxx>
#ifdef _WIN32
#include <ROOT/RRawFileWin.hxx>
#else
#include <ROOT/RRawFileUnix.hxx>
#endif

#include "TPluginManager.h"
#include "TROOT.h"


ROOT::Internal::RRawFile *ROOT::Internal::RRawFile::RawFilePointer(std::string_view url, ROptions options)
{
   std::string transport = GetTransport(url);
   if (transport == "http" || transport == "https") {
      ROOT::Internal::RRawFile *fRawFileHelper = nullptr;
      if (TPluginHandler *h = gROOT->GetPluginManager()->FindHandler("ROOT::Internal::RRawFile")) {
         if (h->LoadPlugin() == 0) {
            fRawFileHelper = (ROOT::Internal::RRawFile *)(h->ExecPlugin(2, &url, &options));
            fRawFileHelper->InitHelper(fRawFileHelper);
         }
         throw std::runtime_error("Cannot load plugin handler for RRawFileDavix");
      }
      throw std::runtime_error("Cannot find plugin handler for RRawFileDavix");
   }
   throw std::runtime_error("Unsupported transport protocol: " + transport);
}
