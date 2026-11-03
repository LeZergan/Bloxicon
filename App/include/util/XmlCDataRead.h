#pragma once

#include <stdexcept>
#include <streambuf>
#include <string>

namespace RBX {
// Read after <![CDATA[ and consume its closing delimiter. Batch string appends
// avoid a formatted stream operation for each script byte. Keep the returned
// allocation compact, as in the previous stream.str(), for large script trees.
inline std::string readXmlCData(std::streambuf& input)
{
    char tail[3];
    for (unsigned i=0;i<3;++i) {
        const auto c=input.sbumpc();
        if (c==std::char_traits<char>::eof()) throw std::runtime_error("Unterminated XML CDATA");
        tail[i]=std::char_traits<char>::to_char_type(c);
    }
    std::string text;
    char block[512];size_t used=0;
    while (tail[0]!=']' || tail[1]!=']' || tail[2]!='>') {
        block[used++]=tail[0];
        if (used==sizeof(block)) {text.append(block,used);used=0;}
        tail[0]=tail[1];tail[1]=tail[2];
        const auto c=input.sbumpc();
        if (c==std::char_traits<char>::eof()) throw std::runtime_error("Unterminated XML CDATA");
        tail[2]=std::char_traits<char>::to_char_type(c);
    }
    text.append(block,used);
    if (text.capacity()>text.size()) return std::string(text.data(),text.size());
    return text;
}
}
