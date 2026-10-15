#include "common.hpp"
#include "exception_observer.hpp"
#include <iostream>
#include <limits>

int main() {
    using namespace osw::spike;
    int checks = 0;
    auto check = [&](bool condition) {
        ++checks;
        if (!condition)
            throw std::runtime_error("Token contract failed");
    };
    auto invalid = [&](std::string_view value) {
        try {
            (void)parse_token(value);
        } catch (const Error &e) {
            check(e.code == ErrorCode::token);
            return;
        }
        check(false);
    };
    try {
        {
            ExceptionObserver exceptions;
            for (const auto invalid_name : {"", "bad\x01" "name", " Lead", "Trail ", "\xff",
                                            "\xe2\x82", "12345678901234567"})
                check(!osw::core::valid_online_id(invalid_name));
            for (const auto valid_name : {"Player_1", "!bad", "Pierluigi Z.", "\xe3\x83\x94\xe3\x82\xa8\xe3\x83\xbc\xe3\x83\xab"})
                check(osw::core::valid_online_id(valid_name));
            check(osw::core::online_id_from_persona("  Pierluigi Z. \xf0\x9f\x8f\x8e ", 7) == "Pierluigi Z.");
            check(osw::core::online_id_from_persona("Pi\xc3\xa9 \xf0\x9f\x8f\x8e", 7) == "Pi\xc3\xa9 \xf0\x9f\x8f\x8e");
            check(osw::core::online_id_from_persona("Mario\x01" " Rossi Bianchi", 7) == "Mario Rossi Bian");
            check(osw::core::online_id_from_persona("123456789012345\xc3\xa9", 7) == "123456789012345");
            check(osw::core::online_id_from_persona("\x01\xff", 1234567) == "Player34567");
            for (const auto invalid_id : {"", "0", "01", "-1", "18446744073709551616"})
                check(!osw::core::parse_decimal_id(invalid_id));
            check(osw::core::parse_decimal_id("18446744073709551615") ==
                  std::numeric_limits<std::uint64_t>::max());
            Token bounded{1, std::vector<std::uint8_t>(2040), "Player"};
            const auto accepted = osw::core::try_encode_token(bounded);
            check(accepted && accepted->size() == 4096);
            bounded.bytes.push_back(0);
            check(!osw::core::try_encode_token(bounded));
            bounded.bytes.clear();
            check(!osw::core::try_encode_token(bounded));
            check(exceptions.quiet());
        }
        Token original{std::numeric_limits<std::uint64_t>::max(), {0, 1, 127, 128, 255}, "OswTest"};
        auto encoded = encode_token(original);
        auto decoded = parse_token(encoded);
        check(decoded.user == original.user && decoded.bytes == original.bytes &&
              decoded.online_id == original.online_id);
        check(encoded == "osw1.18446744073709551615.00017f80ff.T3N3VGVzdA");
        check(parse_token("osw1.1.AaFF.T3N3VGVzdA").bytes == std::vector<std::uint8_t>({170, 255}));
        Token unicode{7, {1}, "Pi\xc3\xa9 Z. \xf0\x9f\x8f\x8e"};
        check(parse_token(encode_token(unicode)).online_id == unicode.online_id);
        for (auto value :
             {"", "osw1", "osw2.1.01.T3N3VGVzdA", "osw1.0.01.T3N3VGVzdA", "osw1.01.01.T3N3VGVzdA",
              "osw1.-1.01.T3N3VGVzdA", "osw1.18446744073709551616.01.T3N3VGVzdA",
              "osw1.1.0.T3N3VGVzdA", "osw1.1.gg.T3N3VGVzdA", "osw1.1..T3N3VGVzdA",
              "osw1.1.01.T3N3VGVzdA=", "osw1.1.01.T3N3VGVzdB", "osw1.1.01.T3N3VGVzdA.extra",
              "osw1.1.01.AA", "osw1.1.01.T3N3VGVzdA\n"})
            invalid(value);
        invalid(std::string(4097, 'a'));
        original.bytes.resize(2048);
        try {
            (void)encode_token(original);
            check(false);
        } catch (const Error &e) {
            check(e.code == ErrorCode::token);
        }
        std::cout << checks << " token contract checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        return report_error(e);
    }
}
