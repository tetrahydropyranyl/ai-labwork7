#pragma once

#include <vector>
#include <string>
#include <functional>
#include <optional>
#include <variant>
#include <expected>
#include <unordered_map>
#include <cstdint>

#include "utilities/logger.h"
#include "utilities/utils.h"
#include "marshalling/definitions.h"

namespace nmarshall {

    class Validator {
      public:

        struct ValidationResult {
            std::optional<std::vector<RawArgument>> args;
            size_t overload_id = 0;
            std::string command_name;
        };

        explicit Validator(std::ostream& log_out)
            : logger_(log_out)
        {}

        void Register(const CommandSchema& schema) {
            schemas_.push_back(schema);
            overloads_[details::ToLower(schema.name)] += 1;
        }

        ValidationResult Validate(
            const std::vector<std::string>& sentence
        ) {

            if (sentence.empty()) {
                return {std::nullopt};
            }
            
            std::string name;
            size_t words_cnt;

            for (words_cnt = sentence.size(); words_cnt > 0; --words_cnt) {
                std::string candidate = sentence[0];
                for (size_t i = 1; i < words_cnt; ++i) {
                    candidate += ' ' + sentence[i];
                }
                auto lowered = ::details::ToLower(candidate);
                if (overloads_.contains(lowered) && overloads_[lowered] != 0) {
                    name = ::details::ToLower(candidate);
                    break;
                }
            }

            if (name.empty()) {
                logger_.LogError(sentence[0] + " is an unknown command");
                return {std::nullopt};
            }

            std::vector<std::string> compressed_sentence = {name};
            for (size_t i = words_cnt; i < sentence.size(); ++i) {
                compressed_sentence.push_back(sentence[i]);
            }

            size_t matched = 0;

            for (const auto& schema : schemas_) {
                if (details::ToLower(schema.name) == name) {
                    LoggerSilencer silencer(logger_, overloads_[name] > 1);
                    compressed_sentence[0] = schema.name;
                    auto attempt = Validate(compressed_sentence, schema);
                    if (attempt.has_value()) {
                        return {attempt.value(), matched, schema.name};
                    }
                    matched++;
                }
            }

            if (overloads_[name] > 1) {
                logger_.LogError("No matching overload for '" + name + "'");
            }

            return {std::nullopt};

        }

      private:
        Logger logger_;
        std::vector<CommandSchema> schemas_;
        std::unordered_map<std::string, size_t> overloads_;

        std::optional<std::vector<RawArgument>> Validate(
            const std::vector<std::string>& sentence, 
            const CommandSchema& schema
        ) {

            std::vector<bool> consumed(sentence.size());
            consumed[0] = true;
            auto non_positional = ValidateNonPositionals(sentence, consumed, schema);

            if (!non_positional.has_value()) {
                return std::nullopt;
            }

            return ValidatePositionals(non_positional.value(), sentence, consumed, schema);
            
        }

        std::expected<RawArgument, uint8_t> ValidateNonPositional(
            const std::vector<std::string>& sentence,
            std::vector<bool>& consumed,
            const ArgumentSchema& schema
        ) {
            std::optional<RawArgument> result;
            size_t word_ptr = 1;

            while (word_ptr < sentence.size()) {
                if (!consumed[word_ptr] && 
                    ((!schema.name.empty() && sentence[word_ptr] == schema.name) ||
                    (schema.name.empty() && schema.restrictions[0].checker(sentence[word_ptr])))
                ) {
                    if (result.has_value()) {
                        logger_.LogError("Token '" + sentence[word_ptr] + "' refedines an already declared argument");
                        return std::unexpected<uint8_t>(1);
                    }
                    if (!schema.name.empty()) {
                        consumed[word_ptr] = true;
                        word_ptr += 1;
                    }
                    std::optional<RawArgument> arg;
                    if (schema.multiplicity == Multiplicity::kOneOrMany) {
                        arg = ValidateMultiple(sentence, consumed, word_ptr, schema.restrictions, false);
                    } else {
                        arg = ValidateMandatory(sentence, consumed, word_ptr, schema.restrictions, false);
                    }
                    if (!arg.has_value()) {
                        return std::unexpected<uint8_t>(1);
                    }
                    result = std::move(arg);
                } else {
                    word_ptr++;
                }
            }
            return (result.has_value() ? std::expected<RawArgument, uint8_t>(result.value()) : std::unexpected<uint8_t>(0));
        }

        std::optional<std::vector<RawArgument>> ValidateNonPositionals(
            const std::vector<std::string>& sentence, 
            std::vector<bool>& consumed,
            const CommandSchema& schema
        ) {

            std::vector<RawArgument> arguments(schema.arguments.size());

            for (size_t i = 0; i < schema.arguments.size(); ++i) {
                const auto& arg_schema = schema.arguments[i];
                if (arg_schema.position == Position::kPositional) {
                    continue;
                }
                auto arg = ValidateNonPositional(sentence, consumed, arg_schema);
                if (arg.has_value()) {
                    arguments[i] = std::move(arg.value());
                } else if (arg.error() == 1 || arg_schema.multiplicity != Multiplicity::kZeroOrOne) {
                    if (arg.error() == 0) {
                        logger_.LogError("Missing mandatory non-positional argument '" + arg_schema.name + "'");
                    }
                    return std::nullopt;
                }
            }

            return arguments;

        }

        std::optional<RawArgument> ValidatePositional(
            std::vector<RawArgument>& args,
            const std::vector<std::string>& sentence, 
            size_t& word_ptr,
            std::vector<bool>& consumed,
            const ArgumentSchema& schema
        ) {

            switch(schema.multiplicity) {

                case Multiplicity::kOne : {
                    auto arg = ValidateMandatory(sentence, consumed, word_ptr, schema.restrictions);
                    return arg;
                }

                case Multiplicity::kZeroOrOne : {
                    auto arg = ValidateOptional(sentence, consumed, word_ptr, schema.restrictions);
                    if (!arg.has_value()) {
                        return std::vector<std::string>{};
                    } else {
                        return arg;
                    }
                }

                case Multiplicity::kOneOrMany : {
                    auto arg = ValidateMultiple(sentence, consumed, word_ptr, schema.restrictions);
                    return arg;
                }

                default : {
                    return std::nullopt;
                }

            }

        }

        std::optional<std::vector<RawArgument>> ValidatePositionals(
            std::vector<RawArgument>& args,
            const std::vector<std::string>& sentence, 
            std::vector<bool>& consumed,
            const CommandSchema& schema
        ) {
            
            size_t word_ptr = 1;
            size_t iter = 0;

            for (const auto& arg_schema : schema.arguments) {
                if (arg_schema.position == Position::kPositional) {
                    auto arg = ValidatePositional(args, sentence, word_ptr, consumed, arg_schema);
                    if (!arg.has_value()) {
                        return std::nullopt;
                    } else {
                        args[iter] = std::move(arg.value());
                    }
                }
                iter += 1;
            }

            while (word_ptr < sentence.size()) {
                if (!consumed[word_ptr]) {
                    logger_.LogError("Input contains redundant arguments (starting from '" + sentence[word_ptr] + "')");
                    return std::nullopt;
                }
                word_ptr++;
            }

            return args;

        }

        std::optional<Token> ValidateMandatory (
            const std::vector<std::string>& sentence, 
            std::vector<bool>& consumed,
            size_t& word_ptr,
            const std::vector<Restriction>& restrictions,
            bool skip_consumed = true
        ) {

            std::vector<std::string> result;

            size_t rollback_pos = word_ptr;

            if (skip_consumed) {
                while (word_ptr < sentence.size() && consumed[word_ptr]) {
                    word_ptr++;
                }
            }

            for (const auto& restriction : restrictions) {
                if (word_ptr == sentence.size() || consumed[word_ptr]) {
                    logger_.LogError("Missing mandatory argument");
                    if (!restriction.desc.empty()) {
                        logger_.LogNote("Expected: " + restriction.desc);
                    }
                    word_ptr = rollback_pos;
                    return std::nullopt;
                }
                if (!restriction.checker(sentence[word_ptr])) {
                    logger_.LogError("Restriction is not satisfied for argument '" + sentence[word_ptr] + "'");
                    if (!restriction.desc.empty()) {
                        logger_.LogNote("Expected " + restriction.desc);
                    }
                    word_ptr = rollback_pos;
                    return std::nullopt;
                } else {
                    word_ptr += 1;
                    result.push_back(sentence[word_ptr - 1]);
                }
            }

            std::fill(consumed.begin() + word_ptr - restrictions.size(), consumed.begin() + word_ptr, true);

            return result;
        }

        std::optional<Token> ValidateOptional (
            const std::vector<std::string>& sentence, 
            std::vector<bool>& consumed,
            size_t& word_ptr,
            const std::vector<Restriction>& restrictions,
            bool skip_consumed = false
        ) {
            LoggerSilencer silencer(logger_);
            return ValidateMandatory(sentence, consumed, word_ptr, restrictions, skip_consumed);
        }

        std::optional<RawArgument> ValidateMultiple (
            const std::vector<std::string>& sentence, 
            std::vector<bool>& consumed,
            size_t& word_ptr,
            const std::vector<Restriction>& restriction,
            bool skip_consumed = true
        ) {

            std::vector<Token> result;
            auto arg = ValidateMandatory(sentence, consumed, word_ptr, restriction, skip_consumed);
            if (!arg.has_value()) {
                return std::nullopt;
            }
            result.push_back(arg.value());

            while (true) {
                arg = ValidateOptional(sentence, consumed, word_ptr, restriction, skip_consumed);
                if (arg.has_value()) {
                    result.push_back(arg.value());
                } else {
                    return result;
                }
            }

        }
    };

} // namespace nmarshall