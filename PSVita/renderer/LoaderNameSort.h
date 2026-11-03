#pragma once
#include <algorithm>
#include <cctype>
#include <string>

namespace VitaBrowser {
// Same byte-wise order as lowercasing both names then comparing std::strings,
// without two temporary strings and two complete folds on every sort comparison.
inline bool foldedPlaceNameLess(const std::string& a,const std::string& b)
{
    return std::lexicographical_compare(a.begin(),a.end(),b.begin(),b.end(),
        [](unsigned char x,unsigned char y) {
            return static_cast<unsigned char>(std::tolower(x))<static_cast<unsigned char>(std::tolower(y));
        });
}
}
