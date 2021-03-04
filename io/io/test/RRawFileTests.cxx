#include "RConfigure.h"
#include "ROOT/RRawFile.hxx"
#include "ROOT/RMakeUnique.hxx"

#include "gtest/gtest.h"


using RRawFile = ROOT::Internal::RRawFile;

TEST(RRawFile, Remote)
{
#ifdef R__HAS_DAVIX
   auto f = RRawFile::Create("http://root.cern.ch/files/davix.test");
   std::string line;
   EXPECT_TRUE(f->Readln(line));
   EXPECT_STREQ("Hello, World", line.c_str());
#else
   EXPECT_THROW(RRawFile::Create("http://root.cern.ch/files/davix.test"), std::runtime_error);
#endif
}