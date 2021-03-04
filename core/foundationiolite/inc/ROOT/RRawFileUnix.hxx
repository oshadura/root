// @(#)root/io:$Id$
// Author: Jakob Blomer

/*************************************************************************
 * Copyright (C) 1995-2018, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_RRawFileUnix
#define ROOT_RRawFileUnix

#include <ROOT/RRawFileLocal.hxx>
#include <ROOT/RStringView.hxx>

#include <cstddef>
#include <cstdint>

namespace ROOT {
namespace Internal {

/**
 * \class RRawFileUnix RRawFileUnix.hxx
 * \ingroup IO
 *
 * The RRawFileUnix class uses POSIX calls to read from a mounted file system. Thus the path name can refer,
 * for instance, to a named pipe instead of a regular file.
 */
class RRawFileUnix : public RRawFileLocal {
private:
   int fFileDes;

protected:
   void OpenImpl() final;
   size_t ReadAtImpl(void *buffer, size_t nbytes, std::uint64_t offset) final;
   void ReadVImpl(RIOVec *ioVec, unsigned int nReq) final;
   std::uint64_t GetSizeImpl() final;
   void *MapImpl(size_t nbytes, std::uint64_t offset, std::uint64_t &mapdOffset) final;
   void UnmapImpl(void *region, size_t nbytes) final;

public:
   RRawFileUnix(std::string_view url, RRawFileLocal::ROptions options);
   ~RRawFileUnix();
   std::unique_ptr<RRawFileLocal> Clone() const final;
   int GetFeatures() const final;
   int GetFd() const { return fFileDes; }
};

} // namespace Internal
} // namespace ROOT

#endif
