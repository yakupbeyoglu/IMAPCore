#ifndef IMAP_PARSER_H_
#define IMAP_PARSER_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "imap/response.h"
#include "imap/types.h"

namespace imap {

/**
 * @class ResponseParser
 * @brief Parses raw IMAP server response lines into ParsedResponse objects.
 *
 * Usage model (pull-based / incremental):
 * @code
 *   ResponseParser parser;
 *   parser.Feed(data_from_socket);
 *   while (auto resp = parser.Next()) {
 *     handle(*resp);
 *   }
 * @endcode
 *
 * The parser buffers incomplete data and emits one ParsedResponse per
 * complete server response unit (which may span multiple lines due to
 * literals).
 */
class ResponseParser {
 public:
  ResponseParser() = default;

  /**
   * @brief Appends raw bytes to the internal buffer.
   * @param data The data to feed into the parser.
   */
  void Feed(std::string_view data);

  /**
   * @brief Returns the next fully parsed response, or std::nullopt if more data is needed.
   * @return The next parsed response, or std::nullopt if incomplete.
   * @note Can be called in a loop until it returns std::nullopt.
   */
  std::optional<ParsedResponse> Next();

  /**
   * @brief Returns true when there is at least one complete response ready.
   * @return True if a complete response is ready, false otherwise.
   */
  [[nodiscard]] bool HasPending() const;

  /**
   * @brief Clears all buffered data.
   */
  void Reset();

 private:
  /**
   * @brief Attempts to extract one complete logical line (handling literals).
   * @return The line (without CRLF) or nullopt if incomplete.
   */
  std::optional<std::string> ExtractLine();

  /**
   * @brief Parses a single complete response line.
   * @param line The response line to parse.
   * @return The parsed response.
   */
  static ParsedResponse ParseLine(std::string_view line);

  /**
   * @brief Parses an untagged response line.
   * @param line The untagged response line.
   * @return The parsed untagged response.
   */
  static UntaggedResponse ParseUntagged(std::string_view line);

  /**
   * @brief Parses a numbered untagged response.
   * @param number The sequence number.
   * @param rest The rest of the response line.
   * @return The parsed untagged response.
   */
  static UntaggedResponse ParseNumberedUntagged(uint32_t number, std::string_view rest);

  /**
   * @brief Parses a status response (OK / NO / BAD / PREAUTH / BYE).
   * @param status The response status.
   * @param rest The rest of the response line.
   * @return The parsed status response.
   */
  static StatusResponse ParseStatusResponse(ResponseStatus status, std::string_view rest);

  /**
   * @brief Parses capability data from a response line.
   * @param rest The rest of the response line.
   * @return The parsed capability data.
   */
  static CapabilityData ParseCapability(std::string_view rest);

  /**
   * @brief Parses flags data from a response line.
   * @param rest The rest of the response line.
   * @return The parsed flags data.
   */
  static FlagsData ParseFlags(std::string_view rest);

  /**
   * @brief Parses search data from a response line.
   * @param rest The rest of the response line.
   * @return The parsed search data.
   */
  static SearchData ParseSearch(std::string_view rest);

  /**
   * @brief Parses status data from a response line.
   * @param rest The rest of the response line.
   * @return The parsed status data.
   */
  static StatusData ParseStatus(std::string_view rest);

  /**
   * @brief Parses a LIST or LSUB response line.
   * @param is_lsub True if LSUB, false if LIST.
   * @param rest The rest of the response line.
   * @return The parsed list data.
   */
  static ListData ParseListLine(bool is_lsub, std::string_view rest);

  /**
   * @brief Parses a FETCH response line.
   * @param seq_num The sequence number.
   * @param rest The rest of the response line.
   * @return The parsed fetched message.
   */
  static FetchedMessage ParseFetch(uint32_t seq_num, std::string_view rest);

  /**
   * @brief Parses an envelope (nested parenthesised list).
   * @param paren_data The parenthesised data.
   * @return The parsed envelope.
   */
  static Envelope ParseEnvelope(std::string_view paren_data);

  /**
   * @brief Parses a list of addresses from parenthesised data.
   * @param paren_data The parenthesised data.
   * @return The parsed address list.
   */
  static std::vector<AddressInfo> ParseAddressList(std::string_view paren_data);

  /**
   * @brief Parses a single address from parenthesised data.
   * @param paren_data The parenthesised data.
   * @return The parsed address info.
   */
  static AddressInfo ParseAddress(std::string_view paren_data);

  /**
   * @brief Parses a body structure from parenthesised data.
   * @param paren_data The parenthesised data.
   * @return The parsed body structure part.
   */
  static BodyStructurePart ParseBodyStructure(std::string_view paren_data);

  // Response code parser — extracts "[CODE args]" if present.
  static ResponseCodeData ParseResponseCode(std::string_view& text);

  // Utility: maps string token to ResponseStatus.
  static ResponseStatus ToResponseStatus(std::string_view token);

  // Utility: maps string token to ResponseCode.
  static ResponseCode ToResponseCode(std::string_view token);

  // Utility: maps message flag string to MessageFlag bit.
  static MessageFlags ParseFlagList(std::string_view paren_list);

  // Parenthesised-list tokeniser: splits "(a b c (d e))" into tokens.
  // Returns the tokens at the current nesting level.
  static std::vector<std::string> TokeniseParenList(std::string_view input);

  // Reads a quoted string from `input` starting at position `pos`.
  // Advances `pos` past the closing quote.
  static std::string ReadQuotedString(std::string_view input, std::size_t& pos);

  // Reads an atom (unquoted token) from `input` starting at `pos`.
  static std::string ReadAtom(std::string_view input, std::size_t& pos);

  // Reads NIL or a string (quoted / atom) and returns nullopt for NIL.
  static std::optional<std::string> ReadNilOrString(std::string_view input,
                                                     std::size_t& pos);

  // Skips whitespace.
  static void SkipSp(std::string_view input, std::size_t& pos);

  std::string buffer_;
  // When we encounter a literal {n} we store the byte count here and wait.
  std::optional<std::size_t> pending_literal_size_;
};

}  // namespace imap

#endif  // IMAP_PARSER_H_
