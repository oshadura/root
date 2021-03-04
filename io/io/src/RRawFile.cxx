// @(#)root/io:$Id$
// Author: Oksana Shadura

/*************************************************************************
 * Copyright (C) 1995-2021, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include <ROOT/RConfig.h>
#include <ROOT/RRawFile.hxx>

#include "TPluginManager.h"
#include "TROOT.h"

std::unique_ptr<ROOT::Internal::RRawFile> ROOT::Internal::RRawFile::Create(std::string_view url, ROOT::Internal::RRawFileLocal::ROptions options)
{  
   std::string transport = ROOT::Internal::RRawFile::GetTransport(url);
   if (transport == "file")  {
      ROOT::Internal::RRawFile::Create(url, options);
   } else if (transport == "http" || transport == "https") {
      if (TPluginHandler *h = gROOT->GetPluginManager()->FindHandler("ROOT::Internal::RRawFile")) {
         if (h->LoadPlugin() == 0) {
            return std::unique_ptr<ROOT::Internal::RRawFile>(reinterpret_cast<ROOT::Internal::RRawFile *>(h->ExecPlugin(2, &url, &options)));;
         }
         throw std::runtime_error("Cannot load plugin handler for RRawFileDavix");
      }
      throw std::runtime_error("Cannot find plugin handler for RRawFileDavix");
   }
   throw std::runtime_error("Unsupported transport protocol: " + transport);
}
