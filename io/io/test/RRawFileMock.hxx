// @(#)root/io:$Id$
// Author: Jakob Blomer

/*************************************************************************
 * Copyright (C) 1995-2018, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_RRawRemoteFileMock
#define ROOT_RRawRemoteFileMock

#include "ROOT/RRawFile.hxx"

using RRawFile = ROOT::Internal::RRawFile;


namespace {

/**
 * A minimal RRawFile implementation that serves data from a string. It keeps a counter of the number of read calls
 * to help veryfing the buffer logic in the base class.
 */
class RRawFileMock : public RRawFile {
public:
   std::string fContent;
   unsigned fNumReadAt;

   RRawFileMock(const std::string &content, RRawFile::ROptions options)
     : RRawFile("", options), fContent(content), fNumReadAt(0) { }

   std::unique_ptr<RRawFile> Clone() const final {
      return std::make_unique<RRawFileMock>(fContent, fOptions);
   }

   void OpenImpl() final
   {
   }

   size_t ReadAtImpl(void *buffer, size_t nbytes, std::uint64_t offset) final
   {
      fNumReadAt++;
      if (offset > fContent.length())
         return 0;

      auto slice = fContent.substr(offset, nbytes);
      memcpy(buffer, slice.data(), slice.length());
      return slice.length();
   }

   std::uint64_t GetSizeImpl() final { return fContent.size(); }

   int GetFeatures() const final { return kFeatureHasSize; }
};

} // anonymous namespace

#endif