#include "decompression.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>
#if TIGHTROPE_HAS_ZSTD_DECOMPRESSION
#include <zstd.h>
#endif
#if TIGHTROPE_HAS_BROTLI_DECOMPRESSION
#include <brotli/decode.h>
#endif

#include "logging/logger.h"
#include "text/ascii.h"
#include "text/json_escape.h"

namespace tightrope::server::middleware {

namespace {

std::string dashboard_error_json(const std::string_view code, const std::string_view message) {
    const std::string code_str(code);
    const std::string message_str(message);
    return std::string(R"({"error":{"code":)") + core::text::quote_json_string(code_str) + R"(,"message":)" +
           core::text::quote_json_string(message_str) + "}}";
}

std::string find_header_case_insensitive(
    const proxy::openai::HeaderMap& headers,
    const std::string_view header_name
) {
    for (const auto& [key, value] : headers) {
        if (core::text::equals_case_insensitive(key, header_name)) {
            return value;
        }
    }
    return {};
}

bool header_value_contains_case_insensitive(
    const proxy::openai::HeaderMap& headers,
    const std::string_view header_name,
    const std::string_view needle
) {
    const auto value = find_header_case_insensitive(headers, header_name);
    if (value.empty()) {
        return false;
    }
    const auto value_lower = core::text::to_lower_ascii(value);
    const auto needle_lower = core::text::to_lower_ascii(needle);
    return value_lower.find(needle_lower) != std::string::npos;
}

std::vector<std::string> parse_content_encodings(const std::string_view value) {
    std::vector<std::string> encodings;
    std::size_t start = 0;
    while (start < value.size()) {
        const auto comma = value.find(',', start);
        const auto end = comma == std::string_view::npos ? value.size() : comma;
        auto token = core::text::trim_ascii(value.substr(start, end - start));
        const auto params = token.find(';');
        if (params != std::string_view::npos) {
            token = core::text::trim_ascii(token.substr(0, params));
        }
        if (!token.empty()) {
            encodings.push_back(core::text::to_lower_ascii(token));
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return encodings;
}

bool looks_like_zstd_frame(const std::string& body) {
    if (body.size() < 4) {
        return false;
    }
    const auto byte0 = static_cast<unsigned char>(body[0]);
    const auto byte1 = static_cast<unsigned char>(body[1]);
    const auto byte2 = static_cast<unsigned char>(body[2]);
    const auto byte3 = static_cast<unsigned char>(body[3]);
    return byte0 == 0x28 && byte1 == 0xB5 && byte2 == 0x2F && byte3 == 0xFD;
}

bool looks_like_gzip_frame(const std::string& body) {
    if (body.size() < 2) {
        return false;
    }
    const auto byte0 = static_cast<unsigned char>(body[0]);
    const auto byte1 = static_cast<unsigned char>(body[1]);
    return byte0 == 0x1F && byte1 == 0x8B;
}

std::string inflate_with_window_bits(const std::string& input, const int window_bits, const std::size_t max_size) {
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    if (inflateInit2(&stream, window_bits) != Z_OK) {
        throw std::runtime_error("inflate init failed");
    }

    std::string output;
    output.reserve(std::min<std::size_t>(input.size() * 2u, max_size));
    std::array<char, 64 * 1024> buffer{};

    int rc = Z_OK;
    while (rc == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        rc = inflate(&stream, Z_NO_FLUSH);
        const auto produced = buffer.size() - stream.avail_out;
        if (produced > 0) {
            if (output.size() + produced > max_size) {
                inflateEnd(&stream);
                throw std::length_error("decompressed body exceeds max size");
            }
            output.append(buffer.data(), produced);
        }
    }

    inflateEnd(&stream);
    if (rc != Z_STREAM_END) {
        throw std::runtime_error("inflate failed");
    }
    return output;
}

std::string decompress_one(const std::string& input, const std::string_view encoding, const std::size_t max_size) {
    if (encoding == "identity") {
        return input;
    }
#if TIGHTROPE_HAS_ZSTD_DECOMPRESSION
    if (encoding == "zstd" || encoding == "zst") {
        ZSTD_DCtx* dctx = ZSTD_createDCtx();
        if (dctx == nullptr) {
            throw std::runtime_error("zstd context allocation failed");
        }

        std::string output;
        output.reserve(std::min<std::size_t>(input.size() * 2u, max_size));
        std::array<char, 64 * 1024> buffer{};

        ZSTD_inBuffer input_buffer{
            .src = input.data(),
            .size = input.size(),
            .pos = 0,
        };
        std::size_t rc = 1;
        while (input_buffer.pos < input_buffer.size) {
            ZSTD_outBuffer output_buffer{
                .dst = buffer.data(),
                .size = buffer.size(),
                .pos = 0,
            };
            rc = ZSTD_decompressStream(dctx, &output_buffer, &input_buffer);
            if (ZSTD_isError(rc)) {
                ZSTD_freeDCtx(dctx);
                throw std::runtime_error("zstd decompress failed");
            }

            if (output_buffer.pos > 0) {
                if (output.size() + output_buffer.pos > max_size) {
                    ZSTD_freeDCtx(dctx);
                    throw std::length_error("decompressed body exceeds max size");
                }
                output.append(buffer.data(), output_buffer.pos);
            }
        }

        ZSTD_freeDCtx(dctx);
        if (rc != 0) {
            throw std::runtime_error("zstd stream incomplete");
        }
        return output;
    }
#endif
 #if TIGHTROPE_HAS_BROTLI_DECOMPRESSION
    if (encoding == "br") {
        BrotliDecoderState* state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
        if (state == nullptr) {
            throw std::runtime_error("brotli context allocation failed");
        }

        std::string output;
        output.reserve(std::min<std::size_t>(input.size() * 2u, max_size));
        std::array<std::uint8_t, 64 * 1024> buffer{};

        const auto* next_in = reinterpret_cast<const std::uint8_t*>(input.data());
        std::size_t avail_in = input.size();

        while (true) {
            auto* next_out = buffer.data();
            std::size_t avail_out = buffer.size();
            const auto rc = BrotliDecoderDecompressStream(state, &avail_in, &next_in, &avail_out, &next_out, nullptr);
            const auto produced = buffer.size() - avail_out;
            if (produced > 0) {
                if (output.size() + produced > max_size) {
                    BrotliDecoderDestroyInstance(state);
                    throw std::length_error("decompressed body exceeds max size");
                }
                output.append(reinterpret_cast<const char*>(buffer.data()), produced);
            }

            if (rc == BROTLI_DECODER_RESULT_SUCCESS) {
                BrotliDecoderDestroyInstance(state);
                return output;
            }
            if (rc == BROTLI_DECODER_RESULT_ERROR) {
                BrotliDecoderDestroyInstance(state);
                throw std::runtime_error("brotli decompress failed");
            }
            if (rc == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT && avail_in == 0) {
                BrotliDecoderDestroyInstance(state);
                throw std::runtime_error("brotli stream incomplete");
            }
        }
    }
#endif
    if (encoding == "gzip") {
        return inflate_with_window_bits(input, 16 + MAX_WBITS, max_size);
    }
    if (encoding == "deflate") {
        return inflate_with_window_bits(input, MAX_WBITS, max_size);
    }
    throw std::invalid_argument("unsupported content encoding");
}

proxy::openai::HeaderMap strip_content_encoding_headers(const proxy::openai::HeaderMap& headers, const std::size_t body_size) {
    proxy::openai::HeaderMap rewritten;
    rewritten.reserve(headers.size() + 1);
    for (const auto& [key, value] : headers) {
        if (core::text::equals_case_insensitive(key, "content-encoding") ||
            core::text::equals_case_insensitive(key, "content-length")) {
            continue;
        }
        rewritten.emplace(key, value);
    }
    rewritten["content-length"] = std::to_string(body_size);
    return rewritten;
}

std::optional<std::string> maybe_decompress_undeclared_json_body(
    const std::string& body,
    const proxy::openai::HeaderMap& headers,
    const std::size_t max_size
) {
    if (!header_value_contains_case_insensitive(headers, "content-type", "json")) {
        return std::nullopt;
    }
    if (looks_like_zstd_frame(body)) {
        try {
            core::logging::log_event(
                core::logging::LogLevel::Debug,
                "middleware",
                "decompression",
                "undeclared_decompression",
                "encoding=zstd body_size=" + std::to_string(body.size())
            );
            return decompress_one(body, "zstd", max_size);
        } catch (...) {
            return std::nullopt;
        }
    }
    if (looks_like_gzip_frame(body)) {
        try {
            core::logging::log_event(
                core::logging::LogLevel::Debug,
                "middleware",
                "decompression",
                "undeclared_decompression",
                "encoding=gzip body_size=" + std::to_string(body.size())
            );
            return decompress_one(body, "gzip", max_size);
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace

DecompressionResult decompress_request_body(std::string body, proxy::openai::HeaderMap headers, const std::size_t max_size) {
    const auto content_encoding = find_header_case_insensitive(headers, "content-encoding");
    if (content_encoding.empty()) {
        if (const auto sniffed = maybe_decompress_undeclared_json_body(body, headers, max_size); sniffed.has_value()) {
            return {
                .ok = true,
                .status = 200,
                .body = *sniffed,
                .headers = strip_content_encoding_headers(headers, sniffed->size()),
            };
        }
        return {
            .ok = true,
            .status = 200,
            .body = std::move(body),
            .headers = std::move(headers),
        };
    }

    const auto encodings = parse_content_encodings(content_encoding);
    if (encodings.empty()) {
        return {
            .ok = true,
            .status = 200,
            .body = std::move(body),
            .headers = std::move(headers),
        };
    }

    try {
        std::string decompressed = std::move(body);
        for (auto it = encodings.rbegin(); it != encodings.rend(); ++it) {
            decompressed = decompress_one(decompressed, *it, max_size);
        }
        const bool identity_only = std::all_of(encodings.begin(), encodings.end(), [](const std::string& encoding) {
            return encoding == "identity";
        });
        if (identity_only) {
            if (const auto sniffed = maybe_decompress_undeclared_json_body(decompressed, headers, max_size);
                sniffed.has_value()) {
                decompressed = *sniffed;
            }
        }
        const auto body_size = decompressed.size();
        return {
            .ok = true,
            .status = 200,
            .body = std::move(decompressed),
            .headers = strip_content_encoding_headers(headers, body_size),
        };
    } catch (const std::invalid_argument&) {
        return {
            .ok = false,
            .status = 400,
            .error_body = dashboard_error_json("invalid_request", "Unsupported Content-Encoding"),
        };
    } catch (const std::length_error&) {
        return {
            .ok = false,
            .status = 413,
            .error_body = dashboard_error_json("payload_too_large", "Request body exceeds the maximum allowed size"),
        };
    } catch (...) {
        return {
            .ok = false,
            .status = 400,
            .error_body = dashboard_error_json(
                "invalid_request",
                "Request body is compressed but could not be decompressed"
            ),
        };
    }
}

} // namespace tightrope::server::middleware
