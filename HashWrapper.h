//
// Created by PlanarG on 2023/8/21.
//

#ifndef TABLE_HASHWRAPPER_H
#define TABLE_HASHWRAPPER_H

#include <type_traits>
#include <cmath>
#include <climits>
#include <string>
#include <utility>
#include <memory>
#include <typeindex>
#include <any>

using Hash = unsigned long long;

class IHash {
public:
    virtual ~IHash() = default;
    [[nodiscard]] virtual Hash hash() const = 0;
};

class HashWrapper {
private:
    class PlaceHolder {
    public:
        virtual ~PlaceHolder() = default;
        [[nodiscard]] virtual PlaceHolder* clone() const = 0;
        [[nodiscard]] virtual const std::type_info& type() const = 0;
        virtual bool equals(const PlaceHolder *other) const = 0;
    };

    template<typename T, typename std::enable_if<
            std::is_base_of<IHash, T>::value, int>::type = 0>
    class Holder: public PlaceHolder {
    public:
        T inner;

        explicit Holder(const T& value) : inner(value) {}

        [[nodiscard]] PlaceHolder * clone() const override {
            return new Holder(inner);
        }

        [[nodiscard]] const std::type_info & type() const override {
            return typeid(T);
        }

        bool equals(const HashWrapper::PlaceHolder *other) const override {
            if (type() != other->type()) {
                return false;
            }
            return inner == static_cast<const Holder*>(other)->inner;
        }
    };

    PlaceHolder* inner;

public:
    Hash hash;

    template<typename T, typename std::enable_if<
            std::is_base_of<IHash, T>::value, int>::type = 0>
    HashWrapper(T value): inner(new Holder<T>(value)), hash(0) {}

    HashWrapper(const HashWrapper& other): inner(other.inner->clone()), hash(other.hash) {}

    HashWrapper& operator = (const HashWrapper &other) {
        if (this != &other) {
            delete inner;
            inner = other.inner->clone();
            hash = other.hash;
        }
        return *this;
    }

    ~HashWrapper() { delete inner; }

    template<typename T, typename std::enable_if<
            std::is_base_of<IHash, T>::value, int>::type = 0>
    [[nodiscard]] T into() const {
        if (typeid(T) != inner->type()) {
            throw std::bad_cast();
        }
        return static_cast<Holder<T>*>(inner)->inner;
    }

    bool equals(const HashWrapper &other) const {
        return inner->equals(other.inner);
    }

    [[nodiscard]] std::type_index type() const {
        return inner->type();
    }
};

std::unordered_map<std::type_index, std::function<Hash(const HashWrapper&)>> hash_functions;

template<typename T, typename std::enable_if<
        std::is_base_of<IHash, T>::value, int>::type = 0>
void register_hash() {
    hash_functions[std::type_index(typeid(T))] = [](const HashWrapper &v) -> Hash {
        return v.into<T>().hash();
    };
}

class Int: public IHash {
public:
    long long inner;

    explicit Int(long long value): inner(value) {}

    [[nodiscard]] Hash hash() const override {
        if (inner >= 0) return inner;
        return ~inner;
    }

    bool operator == (const Int &b) const {
        return inner == b.inner;
    }
};

void register_hashes() {
    register_hash<Int>();
}

Hash get_hash(HashWrapper &v) {
    if (v.hash) return v.hash;
    auto it = hash_functions.find(v.type());
    if (it != hash_functions.end()) {
        return v.hash = it->second(v);
    }
    throw std::runtime_error("No hash function registered for the given type");
}

using Integer = int64_t;
using Number = double;

enum Tag {
    INT, NUM, STR, PTR, H
};

class Key {
private:
    union {
        Integer i; // integer numbers
        Number n; // float numbers
        std::string s; // strings
        void* p; // userdata, raw pointer
        HashWrapper h; // any type implements IHash
    };

    Tag tag;

    static Hash hash_INT(const Integer &i) {
        return i >= 0 ? i : ~i;
    }

    static Hash hash_NUM(const Number &n) {
        int power;
        auto tail = frexp(n, &power) * -(double)std::numeric_limits<int>::min();
        if (isnan(tail) || isinf(tail))
            return 0;
        return (Hash)tail + power;
    }

    static Hash hash_PTR(void* p) {
        return (Hash)p;
    }

    static Hash hash_STR(const std::string &s) {
        Hash hash = 1829732;
        for (const auto &c: s)
            hash ^= (hash << 5) + (hash >> 2) + c;
        return hash;
    }

    static Hash hash_H(HashWrapper &h) {
        return get_hash(h);
    }

public:
    bool operator == (const Key& other) const {
        if (tag != other.tag)
            return false;
        switch (tag) {
            case INT: return i == other.i;
            case NUM: return n == other.n;
            case STR: return s == other.s;
            case PTR: return p == other.p;
            case H: return h.equals(other.h);
        }
    }

    [[nodiscard]] Integer item() const { return i; }
    
    Key() = delete;

    Key(const Key &other): tag(other.tag) {
        switch (tag) {
            case INT: i = other.i; break;
            case NUM: n = other.n; break;
            case PTR: p = other.p; break;
            case STR: s = other.s; break;
            case H: h = other.h; break;
        }
    }

    template<class T> Key(T value) {
        if constexpr (std::is_integral_v<T>) {
            i = value, tag = INT;
        } else if constexpr (std::is_floating_point_v<T>) {
            n = value, tag = NUM;
        } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, const char *>) {
            new (&s) std::string(value), tag = STR;
        } else if constexpr (std::is_same_v<T, void*>) {
            p = value, tag = PTR;
        } else {
            new (&h) HashWrapper(value), tag = H;
        }
    }
    
    ~Key() {
        switch (tag) {
            case STR:
                s.~basic_string(); break;
            case H:
                h.~HashWrapper(); break;
            case INT: case NUM: case PTR:
                break;
        }
    }

    Hash hash() {
        switch (tag) {
            case INT: return hash_INT(i);
            case NUM: return hash_NUM(n);
            case STR: return hash_STR(s);
            case PTR: return hash_PTR(p);
            case H: return hash_H(h);
        }
    }

    Tag type() { return tag; }
};

#endif //TABLE_HASHWRAPPER_H
