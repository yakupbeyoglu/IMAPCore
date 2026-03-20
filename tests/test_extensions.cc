#include "imap/extensions.h"
#include "imap/types.h"

#include <gtest/gtest.h>

#include <string>

namespace imap {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool HasPrefix(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool HasSuffix(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.substr(s.size() - suffix.size()) == suffix;
}

// ---------------------------------------------------------------------------
// ExtensionCommandBuilder — basic tests
// ---------------------------------------------------------------------------

class ExtensionCommandBuilderTest : public ::testing::Test {
 protected:
  ExtensionCommandBuilder builder_{"A"};
};

TEST_F(ExtensionCommandBuilderTest, TagsIncrement) {
  std::string c1 = builder_.Xlist("", "*");
  std::string c2 = builder_.Unselect();
  EXPECT_NE(c1, c2);
  EXPECT_EQ(builder_.LastTag(), "A2");
}

TEST_F(ExtensionCommandBuilderTest, ResetResetsCounter) {
  builder_.Xlist("", "*");
  builder_.Xlist("", "*");
  builder_.Reset();
  std::string cmd = builder_.Xlist("", "*");
  EXPECT_TRUE(HasPrefix(cmd, "A1 "));
}

// --- XLIST ---

TEST_F(ExtensionCommandBuilderTest, XlistFormat) {
  std::string cmd = builder_.Xlist("", "*");
  EXPECT_TRUE(HasPrefix(cmd, "A1 XLIST"));
  EXPECT_NE(cmd.find("*"), std::string::npos);
  EXPECT_TRUE(HasSuffix(cmd, "\r\n"));
}

TEST_F(ExtensionCommandBuilderTest, XlistWithReference) {
  std::string cmd = builder_.Xlist("INBOX", "%");
  EXPECT_NE(cmd.find("INBOX"), std::string::npos);
  EXPECT_NE(cmd.find("%"), std::string::npos);
}

// --- UNSELECT ---

TEST_F(ExtensionCommandBuilderTest, UnselectFormat) {
  std::string cmd = builder_.Unselect();
  EXPECT_NE(cmd.find("UNSELECT"), std::string::npos);
  EXPECT_TRUE(HasSuffix(cmd, "\r\n"));
}

// --- MOVE ---

TEST_F(ExtensionCommandBuilderTest, MoveFormat) {
  std::string cmd = builder_.Move("1:5", "Archive");
  EXPECT_NE(cmd.find("MOVE"), std::string::npos);
  EXPECT_NE(cmd.find("1:5"), std::string::npos);
  EXPECT_NE(cmd.find("Archive"), std::string::npos);
  EXPECT_TRUE(HasSuffix(cmd, "\r\n"));
}

TEST_F(ExtensionCommandBuilderTest, UidMoveFormat) {
  std::string cmd = builder_.Move("100:200", "Sent", true);
  EXPECT_NE(cmd.find("UID MOVE"), std::string::npos);
}

// --- SELECT CONDSTORE ---

TEST_F(ExtensionCommandBuilderTest, SelectCondStoreFormat) {
  std::string cmd = builder_.SelectCondStore("INBOX");
  EXPECT_NE(cmd.find("SELECT"), std::string::npos);
  EXPECT_NE(cmd.find("INBOX"), std::string::npos);
  EXPECT_NE(cmd.find("CONDSTORE"), std::string::npos);
  EXPECT_TRUE(HasSuffix(cmd, "\r\n"));
}

TEST_F(ExtensionCommandBuilderTest, ExamineCondStoreFormat) {
  std::string cmd = builder_.ExamineCondStore("INBOX");
  EXPECT_NE(cmd.find("EXAMINE"), std::string::npos);
  EXPECT_NE(cmd.find("CONDSTORE"), std::string::npos);
}

// --- FETCH CHANGEDSINCE ---

TEST_F(ExtensionCommandBuilderTest, FetchChangedSinceFormat) {
  std::string cmd = builder_.FetchChangedSince(
      "1:*",
      static_cast<FetchItems>(FetchItem::kUid),
      12345);
  EXPECT_NE(cmd.find("FETCH"), std::string::npos);
  EXPECT_NE(cmd.find("CHANGEDSINCE"), std::string::npos);
  EXPECT_NE(cmd.find("12345"), std::string::npos);
  EXPECT_TRUE(HasSuffix(cmd, "\r\n"));
}

TEST_F(ExtensionCommandBuilderTest, UidFetchChangedSinceUsesUidPrefix) {
  std::string cmd =
      builder_.FetchChangedSince(
          "1:*", static_cast<FetchItems>(FetchItem::kUid), 99, true);
  EXPECT_NE(cmd.find("UID FETCH"), std::string::npos);
}

// --- STORE UNCHANGEDSINCE ---

TEST_F(ExtensionCommandBuilderTest, StoreUnchangedSinceFormat) {
  std::string cmd = builder_.StoreUnchangedSince(
      "1:3", "+FLAGS", static_cast<MessageFlags>(MessageFlag::kSeen), 5000);
  EXPECT_NE(cmd.find("STORE"), std::string::npos);
  EXPECT_NE(cmd.find("UNCHANGEDSINCE"), std::string::npos);
  EXPECT_NE(cmd.find("5000"), std::string::npos);
  EXPECT_NE(cmd.find("\\Seen"), std::string::npos);
  EXPECT_TRUE(HasSuffix(cmd, "\r\n"));
}

// --- SEARCH MODSEQ ---

TEST_F(ExtensionCommandBuilderTest, SearchModSeqFormat) {
  std::string cmd = builder_.SearchModSeq({"ALL"}, 42000);
  EXPECT_NE(cmd.find("SEARCH"), std::string::npos);
  EXPECT_NE(cmd.find("MODSEQ"), std::string::npos);
  EXPECT_NE(cmd.find("42000"), std::string::npos);
  EXPECT_TRUE(HasSuffix(cmd, "\r\n"));
}

TEST_F(ExtensionCommandBuilderTest, UidSearchModSeqFormat) {
  std::string cmd = builder_.SearchModSeq({"ALL"}, 1, true);
  EXPECT_NE(cmd.find("UID SEARCH"), std::string::npos);
}

// --- SELECT QRESYNC ---

TEST_F(ExtensionCommandBuilderTest, SelectQResyncBasic) {
  QResyncParams params{12345, 67890, std::nullopt, std::nullopt};
  std::string cmd = builder_.SelectQResync("INBOX", params);
  EXPECT_NE(cmd.find("SELECT"), std::string::npos);
  EXPECT_NE(cmd.find("QRESYNC"), std::string::npos);
  EXPECT_NE(cmd.find("12345"), std::string::npos);
  EXPECT_NE(cmd.find("67890"), std::string::npos);
  EXPECT_TRUE(HasSuffix(cmd, "\r\n"));
}

TEST_F(ExtensionCommandBuilderTest, SelectQResyncWithKnownUids) {
  QResyncParams params{1, 2, std::string("1:100"), std::nullopt};
  std::string cmd = builder_.SelectQResync("INBOX", params);
  EXPECT_NE(cmd.find("1:100"), std::string::npos);
}

// --- SORT ---

TEST_F(ExtensionCommandBuilderTest, SortFormat) {
  std::string cmd = builder_.Sort({"DATE"}, "UTF-8", {"ALL"});
  EXPECT_NE(cmd.find("SORT"), std::string::npos);
  EXPECT_NE(cmd.find("DATE"), std::string::npos);
  EXPECT_NE(cmd.find("UTF-8"), std::string::npos);
  EXPECT_TRUE(HasSuffix(cmd, "\r\n"));
}

TEST_F(ExtensionCommandBuilderTest, UidSortFormat) {
  std::string cmd = builder_.Sort({"REVERSE", "DATE"}, "UTF-8", {"ALL"}, true);
  EXPECT_NE(cmd.find("UID SORT"), std::string::npos);
  EXPECT_NE(cmd.find("REVERSE"), std::string::npos);
}

// --- ESEARCH ---

TEST_F(ExtensionCommandBuilderTest, ESearchFormat) {
  std::string cmd = builder_.ESearch({"ALL"}, {"MIN", "MAX", "COUNT"});
  EXPECT_NE(cmd.find("SEARCH"), std::string::npos);
  EXPECT_NE(cmd.find("RETURN"), std::string::npos);
  EXPECT_NE(cmd.find("MIN"), std::string::npos);
  EXPECT_NE(cmd.find("MAX"), std::string::npos);
  EXPECT_NE(cmd.find("COUNT"), std::string::npos);
  EXPECT_TRUE(HasSuffix(cmd, "\r\n"));
}

TEST_F(ExtensionCommandBuilderTest, UidESearchFormat) {
  std::string cmd = builder_.ESearch({"ALL"}, {"ALL"}, true);
  EXPECT_NE(cmd.find("UID SEARCH"), std::string::npos);
}

// ---------------------------------------------------------------------------
// SpecialUseFlagFromString
// ---------------------------------------------------------------------------

TEST(SpecialUseFlagTest, SentFlag) {
  SpecialUseFlags f = SpecialUseFlagFromString("\\Sent");
  EXPECT_NE(f & static_cast<SpecialUseFlags>(SpecialUseFlag::kSent), 0);
}

TEST(SpecialUseFlagTest, TrashFlag) {
  SpecialUseFlags f = SpecialUseFlagFromString("\\Trash");
  EXPECT_NE(f & static_cast<SpecialUseFlags>(SpecialUseFlag::kTrash), 0);
}

TEST(SpecialUseFlagTest, DraftsFlag) {
  SpecialUseFlags f = SpecialUseFlagFromString("\\Drafts");
  EXPECT_NE(f & static_cast<SpecialUseFlags>(SpecialUseFlag::kDrafts), 0);
}

TEST(SpecialUseFlagTest, JunkFlag) {
  SpecialUseFlags f = SpecialUseFlagFromString("\\Junk");
  EXPECT_NE(f & static_cast<SpecialUseFlags>(SpecialUseFlag::kJunk), 0);
}

TEST(SpecialUseFlagTest, UnknownAttributeReturnsNone) {
  SpecialUseFlags f = SpecialUseFlagFromString("\\Unknown");
  EXPECT_EQ(f, static_cast<SpecialUseFlags>(SpecialUseFlag::kNone));
}

// ---------------------------------------------------------------------------
// Capability constants sanity check
// ---------------------------------------------------------------------------

TEST(CapConstantsTest, ValuesNotEmpty) {
  EXPECT_FALSE(kCapMove.empty());
  EXPECT_FALSE(kCapCondStore.empty());
  EXPECT_FALSE(kCapQResync.empty());
  EXPECT_FALSE(kCapXList.empty());
  EXPECT_FALSE(kCapESearch.empty());
  EXPECT_FALSE(kCapSortThread.empty());
}

}  // namespace
}  // namespace imap
