#pragma once

#include <array>
#include <cstddef>

namespace RBX {

// Preview the original first-match/fastRemove sequence without changing the
// owner's connectors. Feature generation can throw or nest; the owner remains
// intact until its normal reconciliation pass, and each thread has its own scope.
template<class Connector, std::size_t Capacity>
class ContactFeatureReuse
{
    std::array<Connector*, Capacity> remaining;
    std::size_t count;
    ContactFeatureReuse* previous;

    static ContactFeatureReuse*& active()
    {
        static thread_local ContactFeatureReuse* value = nullptr;
        return value;
    }

public:
    template<class Array>
    explicit ContactFeatureReuse(const Array& originals)
        : count(originals.size()), previous(active())
    {
        for (std::size_t i = 0; i < count; ++i) remaining[i] = originals[i];
        active() = this;
    }

    ~ContactFeatureReuse() { active() = previous; }
    ContactFeatureReuse(const ContactFeatureReuse&) = delete;
    ContactFeatureReuse& operator=(const ContactFeatureReuse&) = delete;

    static ContactFeatureReuse* current() { return active(); }

    Connector* claim(Connector* candidate)
    {
        for (std::size_t i = 0; i < count; ++i)
            if (Connector::match(remaining[i], candidate))
            {
                Connector* answer = remaining[i];
                remaining[i] = remaining[--count];
                return answer;
            }
        return nullptr;
    }
};

}
