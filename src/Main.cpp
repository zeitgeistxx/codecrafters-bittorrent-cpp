#include <iostream>
#include <iomanip>
#include <string>
#include <cstring>
#include <vector>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <openssl/sha.h>

#include "lib/nlohmann/json.hpp"

using json = nlohmann::json;

// Function prototypes
json decode_bencoded_value(const std::string &encoded_value, size_t &position);

bool is_binary_data(const std::string &data)
{
    for (char ch : data)
    {
        if (!std::isprint(ch) && !std::isspace(ch))
        {
            return true;
        }
    }
    return false;
}

json decode_bencoded_string(const std::string &encoded_string, size_t &position)
{
    size_t colon_index = encoded_string.find(':', position);
    if (colon_index != std::string::npos)
    {
        std::string string_size_str = encoded_string.substr(position, colon_index - position);
        int64_t string_size_int = std::atoll(string_size_str.c_str());
        position = colon_index + 1;
        std::string str = encoded_string.substr(position, string_size_int);
        position += string_size_int;

        // treat it like binary data when applicable
        if (is_binary_data(str))
        {
            return json::binary_t(std::vector<unsigned char>(str.begin(), str.end()));
        }
        else
        {
            return json(str);
        }
    }
    else
    {
        throw std::runtime_error("Invalid encoded string: " + encoded_string);
    }
}

json decode_bencoded_integer(const std::string &encoded_number, size_t &position)
{
    position++;
    size_t end = encoded_number.find('e', position);
    if (end == std::string::npos)
    {
        throw std::invalid_argument("Invalid bencoded integer");
    }
    std::string integer_str = encoded_number.substr(position, end - position);
    position = end + 1;
    return std::stoll(integer_str);
}

json decode_bencoded_list(const std::string &encoded_list, size_t &position)
{
    json list = json::array();
    position++;

    while (position < encoded_list.length())
    {
        if (encoded_list[position] == 'e')
        {
            position++;
            return list;
        }

        list.push_back(decode_bencoded_value(encoded_list, position));
    }

    throw std::runtime_error("Invalid list encoding");
}

json decode_bencoded_dictionary(const std::string &encoded_dictionary, size_t &position)
{
    json dict = json::object();
    position++;

    while (position < encoded_dictionary.length())
    {
        if (encoded_dictionary[position] == 'e')
        {
            position++;
            return dict;
        }

        json key = decode_bencoded_string(encoded_dictionary, position);

        dict[key] = decode_bencoded_value(encoded_dictionary, position);
    }

    throw std::runtime_error("Invalid dictionary encoding");
}

json decode_bencoded_value(const std::string &encoded_value, size_t &position)
{
    if (std::isdigit(encoded_value[position]))
    {
        return decode_bencoded_string(encoded_value, position);
    }
    else if (encoded_value[position] == 'i')
    {
        return decode_bencoded_integer(encoded_value, position);
    }
    else if (encoded_value[position] == 'l')
    {
        return decode_bencoded_list(encoded_value, position);
    }
    else if (encoded_value[position] == 'd')
    {
        return decode_bencoded_dictionary(encoded_value, position);
    }
    else
    {
        throw std::runtime_error("Unhandled encoded value: " + encoded_value);
    }
}

json decode_bencoded_value(const std::string &encoded_value)
{
    size_t position = 0;
    return decode_bencoded_value(encoded_value, position);
}

std::string bencode_info_dict(const json &info_dict)
{
    std::ostringstream oss;
    oss << "d";

    // sorted list of keys for lexicographical ordering
    std::vector<std::string> keys;
    for (auto it = info_dict.begin(); it != info_dict.end(); ++it)
    {
        keys.push_back(it.key());
    }
    std::sort(keys.begin(), keys.end());

    for (const auto &key : keys)
    {
        const auto &value = info_dict[key];

        oss << key.length() << ":" << key;

        if (value.is_string())
        {
            oss << value.get<std::string>().length() << ":" << value;
        }
        else if (value.is_number_integer())
        {
            oss << "i" << value.get<int>() << "e";
        }
        else if (value.is_array())
        {
            oss << "l";
            for (const auto &item : value)
            {
                oss << item.get<std::string>().length() << ":" << item;
            }
            oss << "e";
        }
        else if (value.is_object())
        {
            oss << bencode_info_dict(value);
        }
        else if (value.is_binary())
        {
            const auto &binary_data = value.get_binary();
            oss << binary_data.size() << ":";
            oss.write(reinterpret_cast<const char *>(binary_data.data()), binary_data.size());
        }
    }
    oss << "e";

    return oss.str();
}

std::string calculate_info_hash(const std::string &bencoded_info_dict)
{
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char *>(bencoded_info_dict.c_str()), bencoded_info_dict.length(), hash);

    std::ostringstream hex_stream;

    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
    {
        hex_stream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }

    return hex_stream.str();
}

void parse_torrent_file(const std::string &filename)
{
    std::ifstream ifs(filename, std::ios::binary);

    if (!ifs.is_open())
    {
        throw std::runtime_error("Unable to find file: " + filename);
    }

    std::string torrent_data((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());

    ifs.close();

    auto decoded_data = decode_bencoded_value(torrent_data);

    std::string tracker_url;
    decoded_data["announce"].get_to(tracker_url);
    std::cout << "Tracker URL: " << tracker_url << std::endl;

    std::cout << "Length: " << decoded_data["info"]["length"] << std::endl;

    auto bencoded_info = bencode_info_dict(decoded_data["info"]);
    auto info_hash = calculate_info_hash(bencoded_info);
    std::cout << "Info Hash: " << info_hash << std::endl;
}

int main(int argc, char *argv[])
{
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " decode <encoded_value>" << std::endl;
        return 1;
    }

    std::string command = argv[1];

    if (command == "decode")
    {
        if (argc < 3)
        {
            std::cerr << "Usage: " << argv[0] << " decode <encoded_value>" << std::endl;
            return 1;
        }
        std::string encoded_value = argv[2];
        json decoded_value = decode_bencoded_value(encoded_value);
        std::cout << decoded_value.dump() << std::endl;
    }
    else if (command == "info")
    {
        if (argc < 3)
        {
            std::cerr << "Usage: " << argv[0] << " decode <encoded_value>" << std::endl;
            return 1;
        }
        std::string filename = argv[2];
        parse_torrent_file(filename);
    }
    else
    {
        std::cerr << "unknown command: " << command << std::endl;
        return 1;
    }

    return 0;
}
