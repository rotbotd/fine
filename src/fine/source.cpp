#include "source.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fine {
    namespace {

        constexpr std::array<std::uint32_t, 64> constants = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
        };

        std::uint32_t rotate_right(std::uint32_t value, unsigned count) {
            return (value >> count) | (value << (32 - count));
        }

        std::string sha256(std::string_view source) {
            std::array<std::uint32_t, 8> state = {
                0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
            };
            std::size_t padded = source.size() + 1;
            while (padded % 64 != 56)
                ++padded;
            std::string bytes(source);
            bytes.push_back(static_cast<char>(0x80));
            bytes.resize(padded, '\0');
            std::uint64_t bits = static_cast<std::uint64_t>(source.size()) * 8;
            for (int shift = 56; shift >= 0; shift -= 8)
                bytes.push_back(static_cast<char>((bits >> shift) & 0xff));

            for (std::size_t block = 0; block < bytes.size(); block += 64) {
                std::array<std::uint32_t, 64> words{};
                for (unsigned i = 0; i < 16; ++i) {
                    auto at = [&](unsigned offset) {
                        return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[block + 4 * i + offset]));
                    };
                    words[i] = (at(0) << 24) | (at(1) << 16) | (at(2) << 8) | at(3);
                }
                for (unsigned i = 16; i < 64; ++i) {
                    std::uint32_t s0 =
                        rotate_right(words[i - 15], 7) ^ rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
                    std::uint32_t s1 =
                        rotate_right(words[i - 2], 17) ^ rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);
                    words[i] = words[i - 16] + s0 + words[i - 7] + s1;
                }

                std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
                std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
                for (unsigned i = 0; i < 64; ++i) {
                    std::uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
                    std::uint32_t choose = (e & f) ^ (~e & g);
                    std::uint32_t temp1 = h + sum1 + choose + constants[i] + words[i];
                    std::uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
                    std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
                    std::uint32_t temp2 = sum0 + majority;
                    h = g;
                    g = f;
                    f = e;
                    e = d + temp1;
                    d = c;
                    c = b;
                    b = a;
                    a = temp1 + temp2;
                }
                state[0] += a;
                state[1] += b;
                state[2] += c;
                state[3] += d;
                state[4] += e;
                state[5] += f;
                state[6] += g;
                state[7] += h;
            }

            std::ostringstream output;
            output << "sha256:" << std::hex << std::setfill('0');
            for (std::uint32_t word : state)
                output << std::setw(8) << word;
            return output.str();
        }

        std::string fresh_document_id() {
            std::random_device random;
            std::ostringstream output;
            output << "document:" << std::hex << std::setfill('0');
            auto tick = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            output << std::setw(16) << static_cast<std::uint64_t>(tick);
#ifdef _WIN32
            output << std::setw(8) << static_cast<unsigned>(_getpid());
#else
            output << std::setw(8) << static_cast<unsigned>(getpid());
#endif
            for (unsigned i = 0; i < 4; ++i)
                output << std::setw(8) << random();
            return output.str();
        }

    }  // namespace

    std::string exact_content_hash(std::string_view source) {
        return sha256(source);
    }

    SourceSnapshot make_source_snapshot(std::string_view display_name, std::string_view source, std::size_t revision,
                                        std::string document_id) {
        if (document_id.empty())
            document_id = fresh_document_id();
        return {std::move(document_id), revision, exact_content_hash(source), source.size(), std::string(display_name)};
    }

}  // namespace fine
