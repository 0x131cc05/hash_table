//
// Created by PlanarG on 2023/8/21.
//

#ifndef TABLE_TABLE_H
#define TABLE_TABLE_H

#include "HashWrapper.h"
#include <utility>
#include <vector>
#include <optional>

using Value = std::any;

const int MAX_BIT = 64;

class Table {
private:
    struct Node {
        std::shared_ptr<Key> key;
        std::shared_ptr<Value> value;
        Node* next;
        Node* vacancy_next;

        Node(const std::shared_ptr<Key> &key, const std::shared_ptr<Value> &value):
                next(nullptr), vacancy_next(nullptr) {
            this->key = key;
            this->value = value;
        }

        Node(): key(nullptr), value(nullptr), next(nullptr) {}
    };

    class NodeReference {
    private:
        std::shared_ptr<Key> key;
        Table* table;

        class Dummy {};

        static bool is_dummy(const Value& value) {
            return value.type() == typeid(Dummy);
        }

    public:
        NodeReference(const std::shared_ptr<Key> &key, Table* table): key(key), table(table) {}
        ~NodeReference() = default;


        template<typename T>
        NodeReference& operator = (const T &rhs) {
//            static_assert(!std::is_same_v<T, Table>, "can't directly point to a table, use &table instead");

            auto value = std::make_shared<Value>(rhs);

            // assign to null
            if (!value->has_value()) {
                table->erase(key);
                return *this;
            }

            table->insert(key, value);

            return *this;
        }

        template<class T>
        NodeReference& operator += (const T &rhs) {
            into<T>() += rhs;
            return *this;
        }

        template<class T>
        NodeReference& operator -= (const T &rhs) {
            into<T>() -= rhs;
            return *this;
        }

        template<class T>
        NodeReference& operator *= (const T &rhs) {
            into<T>() *= rhs;
            return *this;
        }

        template<class T>
        NodeReference& operator /= (const T &rhs) {
            into<T>() /= rhs;
            return *this;
        }

        std::shared_ptr<Value> unwrap() {
            auto result = table->query(key);
            if (!result.has_value()) {
                auto to_insert = std::make_shared<Value>(Dummy());
                table->insert(key, to_insert);
                return to_insert;
            } else {
                return result.value();
            }
        }

        /// 将值转化为给定类型的引用
        template<class T> T& into() {
            auto &value = *unwrap();
            if (is_dummy(value)) {
                value = T();
            }
            try {
                return std::any_cast<T&>(value);
            } catch (const std::exception &e) {
                throw std::runtime_error("Table: .into() failed when unpacking ");
            }
        }
    };

    std::shared_ptr<Node*> hash;
    std::shared_ptr<std::shared_ptr<Value>*> array;
    Node* vacancy_head;

    unsigned long array_size_log2; // array 部分的长度关于 2 的对数，至少为 0
    unsigned long hash_size_log2; // hash 部分的长度关于 2 的对数，至少为 1
    unsigned long last_free; // 当前的 free 指针，只会往前移动

    [[nodiscard]] unsigned long hash_size() const {
        return 1 << hash_size_log2;
    }

    [[nodiscard]] unsigned long array_size() const {
        return 1 << array_size_log2;
    }

    Node* main_pos(const std::shared_ptr<Key>& key) {
        return &(*hash)[key->hash() & (hash_size() - 1)];
    }

    static bool is_empty(Node* node) {
        return node == nullptr || node->value == nullptr;
    }

    /// 返回 hash 部分的一个空闲位置，这个位置可能不存在
    std::optional<Node*> get_free_pos() {
        while (vacancy_head != nullptr) {
            if (is_empty(vacancy_head))
                return vacancy_head;
            auto tmp = vacancy_head->vacancy_next;
            vacancy_head->vacancy_next = nullptr;
            vacancy_head = tmp;
        }

        while (last_free >= 0) {
            if ((*hash)[last_free].value == nullptr)
                return &(*hash)[last_free];
            if (last_free == 0)
                return {};
            last_free--;
        }

        return {};
    }

    /// 重新分配 array 部分与 hash 部分的大小
    void resize(unsigned long new_array_size_log2, unsigned long new_hash_size_log2) {
        Table new_table(new_array_size_log2, new_hash_size_log2);

        assert(new_hash_size_log2 >= 1);

        auto boundary = std::min(array_size(), (1ul << new_array_size_log2));

        auto array1 = *new_table.array, array2 = *array;

        for (auto i = 0; i < boundary; i++)
            array1[i] = std::move(array2[i]);

        for (auto i = boundary; i < array_size(); i++)
            if (array2[i]->has_value()) {
                new_table.insert(std::make_shared<Key>(i), array2[i]);
            }

        auto hash2 = *hash;

        for (auto i = 0; i < hash_size(); i++) {
            if (!is_empty(&hash2[i])) {
                auto &key = hash2[i].key;
                if (key->type() == INT && key->item() < (1 << new_array_size_log2)) {
                    array1[key->item()] = std::move(hash2[i].value);
                } else {
                    new_table.insert(hash2[i].key, hash2[i].value);
                }
            }
        }

        std::swap(array, new_table.array);
        std::swap(hash, new_table.hash);

        array_size_log2 = new_array_size_log2;
        hash_size_log2 = new_hash_size_log2;

        vacancy_head = new_table.vacancy_head;
        last_free = new_table.last_free;
    }

    template<class T>
    void clear(std::vector<T> &v) {
        std::vector<T>().swap(v);
    }

    /// 清空并释放 vector
    template<class T, typename... Tails>
    void clear(std::vector<T> &v, Tails& ...tails) {
        clear(v), clear(tails...);
    }

    /// 返回 x 的二进制位数，最低位为第 0 位
    static unsigned bit(unsigned long long x) {
        return MAX_BIT - __builtin_clzll(x) - 1;
    }

    /// 重新计算 array, hash 两个部分的大小，注意一定会有一个新的元素被插入到 hash 中
    void recompute_size() {
        static int counter[MAX_BIT];

        for (int i = 0; i < MAX_BIT; i++)
            counter[i] = 0;

        // There are at lease two occupied positions: 0 and the key to insert.
        // At first hash_part represents number of elements either in array or hash, then we will
        // exclude array size from it.
        unsigned long hash_part = 2;

        auto push_into_counter = [&](long long i) {
            if (i >= 1) counter[bit(i)]++;
        };

        auto array1 = *array; // array deref

        // Skip array[0], which is supposed to be occupied.
        for (int i = 1; i < array_size(); i++)
            if (array1[i] != nullptr && array1[i]->has_value()) {
                push_into_counter(i);
                hash_part++;
            }

        auto hash1 = *hash; // hash deref

        for (int i = 0; i < hash_size(); i++) {
            if (!is_empty(&(*hash)[i])) {
                hash_part++;
                if (hash1[i].key->type() == INT) {
                    push_into_counter(hash1[i].key->item());
                }
            }
        }

        int new_array_size_log2 = 0, array_part = 0;
        for (int i = 0, total = 1; i < std::min(MAX_BIT, 31); i++) {
            total += counter[i];
            // less half vacancy, 1 << (i + 1) can be a new array-part size
            // take the maximum from all possible size
            if (total > (1 << i)) {
                new_array_size_log2 = i + 1;
                array_part = total;
            }
        }

        hash_part -= array_part;
        auto new_hash_size_log2 = std::max(1u, bit(hash_part) + 1);

        resize(new_array_size_log2, new_hash_size_log2);
    }

    void free(Node* node) {
        node->key.reset(), node->value.reset();
        node->next = nullptr, node->vacancy_next = nullptr;
        if (vacancy_head != nullptr)
            node->vacancy_next = vacancy_head;
        vacancy_head = node;
    }

public:
    explicit Table(unsigned long array_size_log2 = 0, unsigned long hash_size_log2 = 1):
        hash_size_log2(hash_size_log2), array_size_log2(array_size_log2),
        vacancy_head(nullptr), last_free((1 << hash_size_log2) - 1) {

        array = std::make_shared<std::shared_ptr<Value>*>(new std::shared_ptr<Value>[1 << array_size_log2]);
        hash = std::make_shared<Node*>(new Node[1 << hash_size_log2]);
    }

    ~Table() {
        if (array.unique())
            delete [] *array;
        if (hash.unique())
            delete [] *hash;
    };

    /// 根据 key 查询，返回对应的 Node 指针
    std::optional<std::shared_ptr<Value>> query(const std::shared_ptr<Key>& key) {
        if (key->type() == INT && key->item() < array_size()) {
            if ((*array)[key->item()] != nullptr)
                return (*array)[key->item()];
            else return {};
        }

        auto mp = main_pos(key);
        while (!is_empty(mp) && !(*mp->key == *key)) {
            mp = mp->next;
        }
        if (is_empty(mp)) {
            return {};
        } else {
            return mp->value;
        }
    }

    /// 从 Table 中删除 entry
    void erase(const std::shared_ptr<Key>& key) {
        if (key->type() == INT && key->item() < array_size()) {
            (*array)[key->item()].reset();
            return;
        }

        Node *mp = main_pos(key), *last = nullptr;
        while (!is_empty(mp) && !(*mp->key == *key)) {
            last = mp, mp = mp->next;
        }
        // nothing to erase
        if (mp == nullptr) {
            return;
        }
        // case #1: both last and mp->next are nullptr. Remove mp directly.
        if (mp->next == nullptr && last == nullptr) {
            free(mp);
            return;
        }
        // case #2: only last is nullptr, move mp->next to mp, then release mp->next
        if (last == nullptr) {
            auto next = mp->next;
            *mp = *next, free(next);
            return;
        }
        // case #3 last is not nullptr, remove mp and update the link
        last->next = mp->next, free(mp);
    }

    /// 向 Table 插入 entry
    void insert(const std::shared_ptr<Key> &key, const std::shared_ptr<Value> &value) {
        if (key->type() == INT && key->item() < array_size()) {
            (*array)[key->item()] = value;
            return;
        }

        auto mp = main_pos(key);
        if (is_empty(mp) || *mp->key == *key) {
            *mp = Node(key, value);
            return;
        }

        auto free_pos = get_free_pos();
        if (!free_pos.has_value()) {
            recompute_size();
            insert(key, value);
            return;
        }

        auto free = free_pos.value();

        if (main_pos(mp->key) == mp) {
            *free = std::move(Node(key, value));
            free->next = mp->next, mp->next = free;
        } else {
            Node *first = main_pos(mp->key), *last = nullptr;
            while (first != mp && first) {
                last = first;
                first = first->next;
            }

            assert(last != nullptr);

            *free = std::move(Node(last->key, last->value));
            free->next = mp->next, last->next = free;
            *mp = std::move(Node(key, value));
        }
    }

    NodeReference operator [] (const Key& key) {
        return { std::make_shared<Key>(key), this };
    }
};

#endif //TABLE_TABLE_H
