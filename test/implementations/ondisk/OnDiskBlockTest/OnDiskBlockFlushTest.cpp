#include <messmer/blockstore/implementations/ondisk/FileAlreadyExistsException.h>
#include <messmer/blockstore/implementations/ondisk/OnDiskBlock.h>
#include <messmer/blockstore/test/testutils/DataBlockFixture.h>
#include "google/gtest/gtest.h"

#include "messmer/tempfile/src/TempFile.h"
#include "messmer/tempfile/src/TempDir.h"

using ::testing::Test;
using ::testing::WithParamInterface;
using ::testing::Values;

using tempfile::TempFile;
using tempfile::TempDir;

using std::unique_ptr;

using namespace blockstore;
using namespace blockstore::ondisk;

namespace bf = boost::filesystem;

class OnDiskBlockFlushTest: public Test, public WithParamInterface<size_t> {
public:
  OnDiskBlockFlushTest()
  // Don't create the temp file yet (therefore pass false to the TempFile constructor)
  : dir(),
    key(Key::FromString("1491BB4932A389EE14BC7090AC772972")),
    file(dir.path() / key.ToString(), false),
    randomData(GetParam()) {
  }
  TempDir dir;
  Key key;
  TempFile file;

  DataBlockFixture randomData;

  unique_ptr<OnDiskBlock> CreateBlockAndLoadItFromDisk() {
    {
      auto block = OnDiskBlock::CreateOnDisk(dir.path(), key, randomData.size());
    }
    return OnDiskBlock::LoadFromDisk(dir.path(), key);
  }

  unique_ptr<OnDiskBlock> CreateBlock() {
    return OnDiskBlock::CreateOnDisk(dir.path(), key, randomData.size());
  }

  void WriteDataToBlock(const unique_ptr<OnDiskBlock> &block) {
    std::memcpy(block->data(), randomData.data(), randomData.size());
  }

  void EXPECT_BLOCK_DATA_CORRECT(const unique_ptr<OnDiskBlock> &block) {
    EXPECT_EQ(randomData.size(), block->size());
    EXPECT_EQ(0, std::memcmp(randomData.data(), block->data(), randomData.size()));
  }

  void EXPECT_STORED_FILE_DATA_CORRECT() {
    Data actual = Data::LoadFromFile(file.path());
    EXPECT_EQ(randomData.size(), actual.size());
    EXPECT_EQ(0, std::memcmp(randomData.data(), actual.data(), randomData.size()));
  }
};
INSTANTIATE_TEST_CASE_P(OnDiskBlockFlushTest, OnDiskBlockFlushTest, Values((size_t)0, (size_t)1, (size_t)1024, (size_t)4096, (size_t)10*1024*1024));

// This test is also tested by OnDiskBlockStoreTest, but there the block is created using the BlockStore interface.
// Here, we create it using OnDiskBlock::CreateOnDisk()
TEST_P(OnDiskBlockFlushTest, AfterCreate_FlushingDoesntChangeBlock) {
  auto block =  CreateBlock();
  WriteDataToBlock(block);
  block->flush();

  EXPECT_BLOCK_DATA_CORRECT(block);
}

// This test is also tested by OnDiskBlockStoreTest, but there the block is created using the BlockStore interface.
// Here, we create it using OnDiskBlock::CreateOnDisk() / OnDiskBlock::LoadFromDisk()
TEST_P(OnDiskBlockFlushTest, AfterLoad_FlushingDoesntChangeBlock) {
  auto block =  CreateBlockAndLoadItFromDisk();
  WriteDataToBlock(block);
  block->flush();

  EXPECT_BLOCK_DATA_CORRECT(block);
}

TEST_P(OnDiskBlockFlushTest, AfterCreate_FlushingWritesCorrectData) {
  auto block = CreateBlock();
  WriteDataToBlock(block);
  block->flush();

  EXPECT_STORED_FILE_DATA_CORRECT();
}

TEST_P(OnDiskBlockFlushTest, AfterLoad_FlushingWritesCorrectData) {
  auto block = CreateBlockAndLoadItFromDisk();
  WriteDataToBlock(block);
  block->flush();

  EXPECT_STORED_FILE_DATA_CORRECT();
}

// This test is also tested by OnDiskBlockStoreTest, but there it can only checks block content by loading it again.
// Here, we check the content on disk.
TEST_P(OnDiskBlockFlushTest, AfterCreate_FlushesWhenDestructed) {
  {
    auto block = CreateBlock();
    WriteDataToBlock(block);
  }
  EXPECT_STORED_FILE_DATA_CORRECT();
}

// This test is also tested by OnDiskBlockStoreTest, but there it can only checks block content by loading it again.
// Here, we check the content on disk.
TEST_P(OnDiskBlockFlushTest, AfterLoad_FlushesWhenDestructed) {
  {
    auto block = CreateBlockAndLoadItFromDisk();
    WriteDataToBlock(block);
  }
  EXPECT_STORED_FILE_DATA_CORRECT();
}