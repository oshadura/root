// @(#)root/io:$Id$
// Author: Oksana Shadura

/*************************************************************************
 * Copyright (C) 1995-2021, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_RRawRemoteFile
#define ROOT_RRawRemoteFile

#include <ROOT/RStringView.hxx>
#include <ROOT/RConfig.h>
#include <ROOT/RRawFileLocal.hxx>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace ROOT {
namespace Internal {

/**
 * \class RRawFile RRawFile.hxx
 * \ingroup IO
 *
 * The RRawFile provides read-only access to remote files. Data can be read either byte-wise or line-wise. This Derived classes provide the
 * low-level read operations from a web server.
 *
 * Files are addressed by URL consisting of a transport protocol part and a location, like https:///path/to/data
 * If the transport protocol part (https or https) and the :// separator are missing, the default protocol is local file. Files are
 * opened when required (on reading, getting file size) and closed on object destruction.
 */


class RRawFile : public ROOT::Internal::RRawFileLocal {

protected:
   void OpenImpl() final override;
   size_t ReadAtImpl(void *buffer, size_t nbytes, std::uint64_t offset) final override;
   std::uint64_t GetSizeImpl() final override;

public:
   RRawFile(std::string_view url, ROptions options): RRawFileLocal(url, options){}
   RRawFile(const RRawFile &) = delete;
   RRawFile &operator=(const RRawFile &) = delete;
   virtual ~RRawFile();

   /// Factory method that returns a suitable concrete implementation according to the transport in the url
   static std::unique_ptr<RRawFile> Create(std::string_view url, ROptions options = ROptions());
}; // class RRawFile

} // namespace Internal
} // namespace ROOT

#endif
