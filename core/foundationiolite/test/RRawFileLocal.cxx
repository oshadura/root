#include "io_test.hxx"
#include "RRawFileLocalMock.hxx"


TEST(RRawFileLocal, Empty)
{
   FileRaii emptyGuard("testEmpty", "");
   auto f = RRawFileLocal::Create("testEmpty");
   EXPECT_TRUE(f->GetFeatures() & RRawFileLocal::kFeatureHasSize);
   EXPECT_EQ(0u, f->GetSize());
   EXPECT_EQ(0u, f->GetFilePos());
   EXPECT_EQ(0u, f->Read(nullptr, 0));
   EXPECT_EQ(0u, f->ReadAt(nullptr, 0, 1));
   std::string line;
   EXPECT_FALSE(f->Readln(line));
}


TEST(RRawFileLocal, Basic)
{
   FileRaii basicGuard("testBasic", "foo\nbar");
   auto f = RRawFileLocal::Create("testBasic");
   EXPECT_EQ(7u, f->GetSize());
   std::string line;
   EXPECT_TRUE(f->Readln(line));
   EXPECT_STREQ("foo", line.c_str());
   EXPECT_TRUE(f->Readln(line));
   EXPECT_STREQ("bar", line.c_str());
   EXPECT_FALSE(f->Readln(line));
   auto clone = f->Clone();
   // file pointer is reset by clone
   EXPECT_TRUE(clone->Readln(line));
   EXPECT_STREQ("foo", line.c_str());
   // Rinse and repeat
   EXPECT_EQ(4U, clone->GetFilePos());
   clone->Seek(0);
   EXPECT_TRUE(clone->Readln(line));
   EXPECT_STREQ("foo", line.c_str());

   auto f2 = RRawFileLocal::Create("NoSuchFile");
   EXPECT_THROW(f2->Readln(line), std::runtime_error);

   auto f3 = RRawFileLocal::Create("FiLE://testBasic");
   EXPECT_EQ(7u, f3->GetSize());

   EXPECT_THROW(RRawFileLocal::Create("://testBasic"), std::runtime_error);
   EXPECT_THROW(RRawFileLocal::Create("Communicator://Kirk"), std::runtime_error);
}

TEST(RRawFileLocal, Readln)
{
   FileRaii linebreakGuard("testLinebreak", "foo\r\none\nline\r\n\r\n");
   auto f = RRawFileLocal::Create("testLinebreak");
   std::string line;
   EXPECT_TRUE(f->Readln(line));
   EXPECT_STREQ("foo", line.c_str());
   EXPECT_TRUE(f->Readln(line));
   EXPECT_STREQ("one\nline", line.c_str());
   EXPECT_TRUE(f->Readln(line));
   EXPECT_TRUE(line.empty());
   EXPECT_FALSE(f->Readln(line));
}


TEST(RRawFileLocal, ReadV)
{
   FileRaii readvGuard("test_rawfile_readv", "Hello, World");
   auto f = RRawFileLocal::Create("test_rawfile_readv");

   char buffer[2];
   buffer[0] = buffer[1] = 0;
   RRawFileLocal::RIOVec iovec[2];
   iovec[0].fBuffer = &buffer[0];
   iovec[0].fOffset = 0;
   iovec[0].fSize = 1;
   iovec[1].fBuffer = &buffer[1];
   iovec[1].fOffset = 11;
   iovec[1].fSize = 2;
   f->ReadV(iovec, 2);

   EXPECT_EQ(1U, iovec[0].fOutBytes);
   EXPECT_EQ(1U, iovec[1].fOutBytes);
   EXPECT_EQ('H', buffer[0]);
   EXPECT_EQ('d', buffer[1]);
}


TEST(RRawFileLocal, SplitUrl)
{
   EXPECT_STREQ("C:\\Data\\events.root", RRawFileLocal::GetLocation("C:\\Data\\events.root").c_str());
   EXPECT_STREQ("///many/slashes", RRawFileLocal::GetLocation("///many/slashes").c_str());
   EXPECT_STREQ("/many/slashes", RRawFileLocal::GetLocation(":///many/slashes").c_str());
   EXPECT_STREQ("file", RRawFileLocal::GetTransport("/foo").c_str());
   EXPECT_STREQ("http", RRawFileLocal::GetTransport("http://").c_str());
   EXPECT_STREQ("", RRawFileLocal::GetLocation("http://").c_str());
   EXPECT_STREQ("http", RRawFileLocal::GetTransport("http://file:///bar").c_str());
}


TEST(RRawFileLocal, ReadDirect)
{
   FileRaii directGuard("testDirect", "abc");
   char buffer;
   RRawFileLocal::ROptions options;
   options.fBlockSize = 0;
   auto f = RRawFileLocal::Create("testDirect");
   EXPECT_EQ(0u, f->Read(&buffer, 0));
   EXPECT_EQ(1u, f->Read(&buffer, 1));
   EXPECT_EQ('a', buffer);
   EXPECT_EQ(1u, f->ReadAt(&buffer, 1, 2));
   EXPECT_EQ('c', buffer);

}


TEST(RRawFileLocal, ReadBuffered)
{
   char buffer[8];
   RRawFileLocal::ROptions options;
   options.fBlockSize = 2;
   std::unique_ptr<RRawFileMock> f(new RRawFileMock("abcdef", options));

   buffer[3] = '\0';
   EXPECT_EQ(3u, f->ReadAt(buffer, 3, 1));
   EXPECT_STREQ("bcd", buffer);
   EXPECT_EQ(1u, f->fNumReadAt); f->fNumReadAt = 0;

   buffer[2] = '\0';
   EXPECT_EQ(2u, f->ReadAt(buffer, 2, 2));
   EXPECT_STREQ("cd", buffer);
   EXPECT_EQ(2u, f->ReadAt(buffer, 2, 0));
   EXPECT_STREQ("ab", buffer);
   EXPECT_EQ(2u, f->ReadAt(buffer, 2, 2));
   EXPECT_STREQ("cd", buffer);
   EXPECT_EQ(2u, f->ReadAt(buffer, 2, 1));
   EXPECT_STREQ("bc", buffer);
   EXPECT_EQ(2u, f->fNumReadAt); f->fNumReadAt = 0;

   EXPECT_EQ(2u, f->ReadAt(buffer, 2, 0));
   EXPECT_STREQ("ab", buffer);
   EXPECT_EQ(1u, f->ReadAt(buffer, 1, 1));
   EXPECT_STREQ("bb", buffer);
   EXPECT_EQ(2u, f->ReadAt(buffer, 2, 1));
   EXPECT_STREQ("bc", buffer);
   EXPECT_EQ(0u, f->fNumReadAt); f->fNumReadAt = 0;
   EXPECT_EQ(2u, f->ReadAt(buffer, 2, 3));
   EXPECT_STREQ("de", buffer);
   EXPECT_EQ(1u, f->fNumReadAt); f->fNumReadAt = 0;
   EXPECT_EQ(1u, f->ReadAt(buffer, 1, 2));
   EXPECT_STREQ("ce", buffer);
   EXPECT_EQ(0u, f->fNumReadAt); f->fNumReadAt = 0;
   EXPECT_EQ(1u, f->ReadAt(buffer, 1, 1));
   EXPECT_STREQ("be", buffer);
   EXPECT_EQ(1u, f->fNumReadAt); f->fNumReadAt = 0;
}


TEST(RRawFileLocal, Mmap)
{
   std::uint64_t mapdOffset;
   std::unique_ptr<RRawFileMock> m(new RRawFileMock("", RRawFileLocal::ROptions()));
   EXPECT_FALSE(m->GetFeatures() & RRawFileLocal::kFeatureHasMmap);
   EXPECT_THROW(m->Map(1, 0, mapdOffset), std::runtime_error);
   EXPECT_THROW(m->Unmap(this, 1), std::runtime_error);

   void *region;
   FileRaii basicGuard("test_rawfile_mmap", "foo");
   auto f = RRawFileLocal::Create("test_rawfile_mmap");
   if (!(f->GetFeatures() & RRawFileLocal::kFeatureHasMmap))
      return;
   region = f->Map(2, 1, mapdOffset);
   auto innerOffset = 1 - mapdOffset;
   ASSERT_NE(region, nullptr);
   EXPECT_EQ("oo", std::string(reinterpret_cast<char *>(region) + innerOffset, 2));
   auto mapdLength = 2 + innerOffset;
   f->Unmap(region, mapdLength);
}
