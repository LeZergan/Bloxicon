#pragma once
#include <array>
#include <cctype>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace VitaBrowser {
// Match the original lowercased name/source/creator/year concatenation, including
// field separators. Build query state once; scan records without temporary text.
// KMP keeps repeated-prefix queries linear even for large catalogues.
class CatalogSearch {
    struct Term {std::string text;std::vector<size_t> prefix;};
    std::array<unsigned char,256> fold;
    std::vector<Term> terms;
    bool contains(const Term& term,const std::array<const std::string*,4>& fields) const {
        size_t matched=0;
        const auto feed=[&](unsigned char raw) {
            const unsigned char c=fold[raw];
            while(matched&&c!=static_cast<unsigned char>(term.text[matched]))matched=term.prefix[matched-1];
            if(c==static_cast<unsigned char>(term.text[matched]))++matched;
            return matched==term.text.size();
        };
        for(size_t i=0;i<fields.size();++i) {
            if(i&&feed(' '))return true;
            for(unsigned char c:*fields[i])if(feed(c))return true;
        }
        return false;
    }
public:
    explicit CatalogSearch(std::string query=std::string()) {
        for(size_t i=0;i<fold.size();++i)fold[i]=static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(i)));
        for(char& c:query)c=static_cast<char>(fold[static_cast<unsigned char>(c)]);
        std::istringstream words(query);std::string word;
        while(words>>word) {
            Term term;term.text=std::move(word);term.prefix.resize(term.text.size());
            for(size_t i=1,matched=0;i<term.text.size();++i) {
                while(matched&&term.text[i]!=term.text[matched])matched=term.prefix[matched-1];
                if(term.text[i]==term.text[matched])++matched;
                term.prefix[i]=matched;
            }
            terms.push_back(std::move(term));
        }
    }
    bool empty() const {return terms.empty();}
    template<class Item> bool matches(const Item& item) const {
        const std::array<const std::string*,4> fields={{&item.name,&item.source,&item.creator,&item.year}};
        for(const auto& term:terms)if(!contains(term,fields))return false;
        return true;
    }
};
}
