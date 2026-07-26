#include "zimodem_hal/arduino/WString.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("String construction and c_str/length", "[string]")
{
    String s1;
    REQUIRE(s1.length() == 0);

    String s2("hello");
    REQUIRE(s2.length() == 5);
    REQUIRE(std::string(s2.c_str()) == "hello");

    String s3('x');
    REQUIRE(std::string(s3.c_str()) == "x");
}

TEST_CASE("String(int/long/unsigned) conversions match Arduino's decimal formatting", "[string]")
{
    REQUIRE(std::string(String(3).c_str()) == "3");
    REQUIRE(std::string(String(-42).c_str()) == "-42");
    REQUIRE(std::string(String(42u).c_str()) == "42");
    REQUIRE(std::string(String(123456789L).c_str()) == "123456789");
    REQUIRE(std::string(String(123456789UL).c_str()) == "123456789");
}

TEST_CASE("assignment from const char*", "[string]")
{
    String s = "first";
    s = "second";
    REQUIRE(std::string(s.c_str()) == "second");
}

TEST_CASE("operator+= for String, const char*, and char", "[string]")
{
    String s = "AT";
    s += "D";
    s += 'T';
    s += String("1234");
    REQUIRE(std::string(s.c_str()) == "ATDT1234");
}

TEST_CASE("operator+ builds new strings without mutating operands (String/const char*/char, both directions)", "[string]")
{
    String base = "hello";
    REQUIRE(std::string((base + " world").c_str()) == "hello world");
    REQUIRE(std::string(("say " + base).c_str()) == "say hello");
    REQUIRE(std::string((base + '!').c_str()) == "hello!");
    REQUIRE(std::string(('!' + base).c_str()) == "!hello");
    REQUIRE(std::string(base.c_str()) == "hello"); // unchanged
}

TEST_CASE("chained concatenation matches the CONNECTED TO banner pattern from zimodem.ino", "[string]")
{
    String wifiSSI = "myssid";
    String ip = "192.168.1.50";
    String result = "CONNECTED TO " + wifiSSI + " (" + ip.c_str() + ")";
    REQUIRE(std::string(result.c_str()) == "CONNECTED TO myssid (192.168.1.50)");
}

TEST_CASE("operator== and operator!= against String and const char*", "[string]")
{
    String s = "abc";
    REQUIRE(s == String("abc"));
    REQUIRE(s == "abc");
    REQUIRE(s != "abd");
    REQUIRE(s != String("abd"));
}

TEST_CASE("equals and equalsIgnoreCase", "[string]")
{
    String s = "CaT";
    REQUIRE(s.equals(String("CaT")));
    REQUIRE(s.equalsIgnoreCase(String("cat")));
    REQUIRE_FALSE(s.equalsIgnoreCase(String("dog")));
}

TEST_CASE("startsWith and endsWith", "[string]")
{
    String s = "/join #channel";
    REQUIRE(s.startsWith(String("/join ")));
    REQUIRE_FALSE(s.startsWith(String("/part")));

    String f = "GAME.PRG";
    REQUIRE(f.endsWith(String(".PRG")));
    REQUIRE_FALSE(f.endsWith(String(".SEQ")));
}

TEST_CASE("indexOf and lastIndexOf for char and String, with fromIndex", "[string]")
{
    String s = "AT+RING 311 ";
    REQUIRE(s.indexOf(' ') == 7);
    REQUIRE(s.indexOf(String(" 311 ")) == 7);
    REQUIRE(s.indexOf(' ', 8) == 11);
    REQUIRE(s.indexOf(String("nope")) == -1);

    String path = "/some/path/file.txt";
    REQUIRE(path.lastIndexOf('/') == 10);
    REQUIRE(path.lastIndexOf(String("/")) == 10);
}

TEST_CASE("substring one-arg and two-arg forms", "[string]")
{
    String s = "0123456789";
    REQUIRE(std::string(s.substring(0, 3).c_str()) == "012");
    REQUIRE(std::string(s.substring(7).c_str()) == "789");
    REQUIRE(std::string(s.substring(3, 3).c_str()) == "");
}

TEST_CASE("remove one-arg (truncate) and two-arg (index,count)", "[string]")
{
    String s = "GET /path/to/resource HTTP/1.1";
    s.remove(0, 4); // matches proto_http.ino's `ln.remove(0,16)`-style header stripping
    REQUIRE(std::string(s.c_str()) == "/path/to/resource HTTP/1.1");

    String t = "keep this part";
    t.remove(4);
    REQUIRE(std::string(t.c_str()) == "keep");
}

TEST_CASE("replace char,char and String,String", "[string]")
{
    String s = "a-b-c";
    s.replace('-', '_');
    REQUIRE(std::string(s.c_str()) == "a_b_c");

    String tmpl = "hello %H world";
    tmpl.replace(String("%H"), String("there"));
    REQUIRE(std::string(tmpl.c_str()) == "hello there world");
}

TEST_CASE("trim removes leading/trailing whitespace only", "[string]")
{
    String s = "  spaced out  ";
    s.trim();
    REQUIRE(std::string(s.c_str()) == "spaced out");

    String allSpace = "   ";
    allSpace.trim();
    REQUIRE(allSpace.length() == 0);
}

TEST_CASE("toLowerCase and toUpperCase mutate in place", "[string]")
{
    String s = "MiXeD";
    s.toLowerCase();
    REQUIRE(std::string(s.c_str()) == "mixed");
    s.toUpperCase();
    REQUIRE(std::string(s.c_str()) == "MIXED");
}

TEST_CASE("operator[] reads characters and returns NUL out of range", "[string]")
{
    String s = "abc";
    REQUIRE(s[0] == 'a');
    REQUIRE(s[2] == 'c');
    REQUIRE(s[99] == '\0');
}

TEST_CASE("non-const operator[] mutates in place, matching zcommand.ino's currentCommand[i]=petToAsc(...)", "[string]")
{
    String s = "abc";
    s[1] = 'X';
    REQUIRE(std::string(s.c_str()) == "aXc");
}

TEST_CASE("non-const operator[] out of range writes to a scratch cell, not undefined behavior", "[string]")
{
    String s = "abc";
    s[99] = 'Z'; // must not crash or corrupt s
    REQUIRE(std::string(s.c_str()) == "abc");
}

TEST_CASE("concat mirrors operator+= for String, const char*, and char", "[string]")
{
    String s = "line";
    REQUIRE(s.concat('!'));
    REQUIRE(std::string(s.c_str()) == "line!");
}
