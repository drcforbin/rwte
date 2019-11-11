#ifndef PDATA_MAP_DETAIL_H
#define PDATA_MAP_DETAIL_H

#include "fmt/format.h"

#include <array>
#include <thread>
#include <variant>

namespace pdata::detail {

using edit_type = std::weak_ptr<std::thread::id>;
using hash_type = std::size_t;

inline uint32_t mask(hash_type hash, uint32_t shift)
{
    return (hash >> shift) & 0x01f;
}

inline uint32_t bitpos(hash_type hash, uint32_t shift)
{
    return 1 << mask(hash, shift);
}

// todo: replace with std::popcount when switched to '20
inline uint32_t popcount(uint32_t x)
{
    return __builtin_popcount(x);
}

inline bool sameEdit(const edit_type& a, const edit_type& b)
{
    // a valid?
    if (auto pa = a.lock()) {
        if (auto pb = b.lock()) {
            // both valid...compare
            return *pa == *pb;
        } else {
            // a valid, b null
            return false;
        }
    } else {
        // return true only if b is null too
        return !b.lock();
    }
}

template <class A, class T>
A setDup(const A& arr, std::size_t idx, const T& val)
{
    auto dup{arr};
    dup[idx] = val;
    return dup;
}

template <class K, class T, class Hash>
class node : public std::enable_shared_from_this<node<K, T, Hash>>
{
public:
    using value_type = std::pair<K, T>;
    using shared_node = std::shared_ptr<node>;
    using entry_type = std::variant<value_type, shared_node>;

    virtual ~node() = default;

    virtual shared_node assoc(uint32_t shift, hash_type hash,
            const entry_type& newEntry, bool& addedLeaf) = 0;
    virtual shared_node without(uint32_t shift, hash_type hash,
            const K& key) = 0;

    virtual std::optional<entry_type> find(uint32_t shift, hash_type hash,
            const K& key) const = 0;

    // transient funcs
    virtual shared_node assoc(const edit_type& edit, uint32_t shift, hash_type hash,
            const entry_type& newEntry, bool& addedLeaf) = 0;
    virtual shared_node without(const edit_type& edit, uint32_t shift, hash_type hash,
            const K& key, bool& removedLeaf) = 0;

    // todo: iterator(handler iterHandler) atom.SeqIterator

    virtual std::string dump(int indent) const = 0;

protected:
    template <typename Derived>
    std::shared_ptr<Derived> shared_from_base()
    {
        return std::static_pointer_cast<Derived>(this->shared_from_this());
    }
};

// forwards
template <class K, class T, class Hash>
class array_node;
template <class K, class T, class Hash>
class hash_collision_node;

template <class K, class T, class Hash>
class bitmap_indexed_node final : public node<K, T, Hash>
{
    using Base = node<K, T, Hash>;
    using hcn_node = hash_collision_node<K, T, Hash>;
    using arr_node = array_node<K, T, Hash>;

public:
    using value_type = typename Base::value_type;
    using shared_node = typename Base::shared_node;
    using entry_type = typename Base::entry_type;

    using array_type = std::vector<entry_type>;

    bitmap_indexed_node(const edit_type& edit) :
        edit(edit)
    {}

    bitmap_indexed_node(const edit_type& edit, uint32_t bitmap, array_type&& array) :
        edit(edit),
        bitmap(bitmap),
        array(std::move(array))
    {}

    shared_node assoc(uint32_t shift, hash_type hash,
            const entry_type& newEntry, bool& addedLeaf)
    {
        auto bit = bitpos(hash, shift);
        auto idx = index(bit);

        // is it maybe already present?
        if (bitmap & bit) {
            auto entry = array[idx];
            if (auto node = std::get_if<shared_node>(&entry)) {
                auto n = (*node)->assoc(shift + 5, hash, newEntry, addedLeaf);
                if (n == *node) {
                    return this->shared_from_this();
                }

                // make a new bitmap_indexed_node, setting the new node in the array
                return std::make_shared<bitmap_indexed_node>(edit_type{}, bitmap,
                        setDup(array, idx, n));
            } else {
                auto value = std::get<value_type>(entry);
                auto newValue = std::get<value_type>(newEntry);

                // same key?
                if (value.first == newValue.first) {
                    if (value.second == newValue.second) {
                        return this->shared_from_this();
                    }

                    // make a new bitmap_indexed_node, setting the item in the array
                    return std::make_shared<bitmap_indexed_node>(edit_type{},
                            bitmap, setDup(array, idx, newEntry));
                }

                // new item, rather than a replacement
                addedLeaf = true;
                return std::make_shared<bitmap_indexed_node>(edit_type{}, bitmap,
                        setDup(array, idx, createNode(shift + 5, value, hash, newValue)));
            }
        } else {
            // not present

            // if we have 16 or more values, promote the node to an array_node
            // containing new bitmap_indexed_node wrapping each value
            auto n = popcount(bitmap);
            if (n >= 16) {
                auto jdx = mask(hash, shift);
                typename arr_node::array_type newArray;
                newArray[jdx] = emptyBin.assoc(shift + 5, hash, newEntry,
                        addedLeaf);

                auto src = array.cbegin();
                for (uint32_t i = 0; i < 32; i++) {
                    if ((bitmap >> i) & 1) {
                        if (auto node = std::get_if<shared_node>(&*src)) {
                            newArray[i] = *node;
                        } else {
                            auto value = std::get<value_type>(*src);
                            newArray[i] = emptyBin.assoc(shift + 5,
                                    Hash{}(value.first), value,
                                    addedLeaf);
                        }
                        src++;
                    }
                }

                return std::make_shared<arr_node>(edit_type{}, n + 1,
                        std::move(newArray));
            } else {
                // insert at idx
                addedLeaf = true;
                // todo...optimize?
                array_type newArray{array};
                newArray.insert(newArray.cbegin() + idx, newEntry);
                return std::make_shared<bitmap_indexed_node>(edit_type{},
                        bitmap | bit, std::move(newArray));
            }
        }
    }

    shared_node without(uint32_t shift, hash_type hash, const K& key)
    {
        auto bit = bitpos(hash, shift);
        if (!(bitmap & bit)) {
            return this->shared_from_this();
        }

        auto idx = index(bit);
        auto entry = array[idx];
        if (auto node = std::get_if<shared_node>(&entry)) {
            auto n = (*node)->without(shift + 5, hash, key);
            if (n == *node) {
                return this->shared_from_this();
            } else if (*node) {
                // make a new bitmap_indexed_node, setting the new node in the array
                return std::make_shared<bitmap_indexed_node>(edit_type{}, bitmap,
                        setDup(array, idx, n));
            } else if (bitmap == bit) {
                return {};
            } else {
                // remove element
                array_type newArray(array.size() - 1);
                auto abegin = array.cbegin();
                std::copy_n(abegin, idx, newArray.begin());
                std::copy(abegin + idx + 1, array.cend(), newArray.begin() + idx);
                return std::make_shared<bitmap_indexed_node>(edit_type{},
                        bitmap ^ bit, std::move(newArray));
            }
        } else {
            auto value = std::get<value_type>(entry);
            if (value.first == key) {
                if (bitmap == bit) {
                    return {};
                }

                // remove element
                array_type newArray(array.size() - 1);
                auto abegin = array.cbegin();
                std::copy_n(abegin, idx, newArray.begin());
                std::copy(abegin + idx + 1, array.cend(), newArray.begin() + idx);
                return std::make_shared<bitmap_indexed_node>(edit_type{},
                        bitmap ^ bit, std::move(newArray));
            }

            return this->shared_from_this();
        }
    }

    std::optional<entry_type> find(uint32_t shift, hash_type hash,
            const K& key) const
    {
        auto bit = bitpos(hash, shift);
        if (bitmap & bit) {
            auto idx = index(bit);
            const auto& entry = array[idx];

            if (auto node = std::get_if<shared_node>(&entry)) {
                return (*node)->find(shift + 5, hash, key);
            } else {
                auto value = std::get<value_type>(entry);
                if (key == value.first) {
                    return entry;
                }
            }
        }

        return std::nullopt;
    }

    shared_node assoc(const edit_type& edit, uint32_t shift, hash_type hash,
            const entry_type& newEntry, bool& addedLeaf)
    {
        auto bit = bitpos(hash, shift);
        auto idx = index(bit);

        // is it maybe already present?
        if (bitmap & bit) {
            auto entry = array[idx];
            if (auto node = std::get_if<shared_node>(&entry)) {
                auto n = (*node)->assoc(edit, shift + 5, hash, newEntry, addedLeaf);
                if (n == *node) {
                    return this->shared_from_this();
                }

                auto editable = ensureEditable(edit);
                editable->array[idx] = n;
                return editable;
            } else {
                auto value = std::get<value_type>(entry);
                auto newValue = std::get<value_type>(newEntry);

                // same key?
                if (value.first == newValue.first) {
                    if (value.second == newValue.second) {
                        return this->shared_from_this();
                    }

                    auto editable = ensureEditable(edit);
                    editable->array[idx] = newValue;
                    return editable;
                }

                // new item, rather than a replacement
                addedLeaf = true;
                auto editable = ensureEditable(edit);
                editable->array[idx] = createNode(edit, shift + 5,
                        value, hash, newValue);
                return editable;
            }
        } else {
            // not present

            auto n = popcount(bitmap);

            // does array have space for this item, or do we have
            // less than 16 elements?
            if (n < array.capacity() || n < 16) {
                auto editable = ensureEditable(edit);

                // insert at idx
                addedLeaf = true;
                editable->array.insert(editable->array.cbegin() + idx, newEntry);
                editable->bitmap |= bit;
                return editable;
            } else {
                // we have 16 or more values, promote the node to an array_node
                // containing a new bitmapIndexedNode with the val in it
                auto jdx = mask(hash, shift);
                typename arr_node::array_type newArray;
                newArray[jdx] = emptyBin.assoc(edit, shift + 5, hash, newEntry,
                        addedLeaf);

                auto src = array.begin();
                for (uint32_t i = 0; i < 32; i++) {
                    if ((bitmap >> i) & 1) {
                        if (auto node = std::get_if<shared_node>(&*src)) {
                            newArray[i] = *node;
                        } else {
                            auto value = std::get<value_type>(*src);
                            newArray[i] = emptyBin.assoc(edit, shift + 5,
                                    Hash{}(value.first), value,
                                    addedLeaf);
                        }
                        src++;
                    }
                }

                return std::make_shared<arr_node>(edit, n + 1,
                        std::move(newArray));
            }
        }
    }

    shared_node without(const edit_type& edit, uint32_t shift, hash_type hash,
            const K& key, bool& removedLeaf)
    {
        auto bit = bitpos(hash, shift);
        if (!(bitmap & bit)) {
            return this->shared_from_this();
        }

        auto idx = index(bit);
        auto entry = array[idx];
        if (auto node = std::get_if<shared_node>(&entry)) {
            auto n = (*node)->without(edit, shift + 5, hash, key, removedLeaf);
            if (n == *node) {
                return this->shared_from_this();
            } else if (*node) {
                auto editable = ensureEditable(edit);
                editable->array[idx] = n;
                return editable;
            } else if (bitmap == bit) {
                return {};
            } else {
                // remove element
                auto editable = ensureEditable(edit);
                editable->bitmap ^= bit;
                editable->array.erase(editable->array.cbegin() + idx);
                return editable;
            }
        } else {
            auto value = std::get<value_type>(entry);
            if (value.first == key) {
                // remove element
                removedLeaf = true;
                auto editable = ensureEditable(edit);
                editable->bitmap ^= bit;
                editable->array.erase(editable->array.cbegin() + idx);
                return editable;
            } else {
                return this->shared_from_this();
            }
        }
    }

    // todo: iterator(handler iterHandler) atom.SeqIterator

    std::string dump(int indent) const
    {
        fmt::memory_buffer msg;

        fmt::format_to(msg, "bin\n");
        for (std::size_t i = 0; i < array.size(); i++) {
            const auto& entry = array[i];
            for (int idt = 0; idt < indent; idt++) {
                fmt::format_to(msg, " ");
            }
            if (auto node = std::get_if<shared_node>(&entry)) {
                fmt::format_to(msg, "{}: {}", i,
                        (*node)->dump(indent + 1));
            } else {
                auto value = std::get<value_type>(entry);
                fmt::format_to(msg, "{}: {}->{}\n", i, value.first, value.second);
            }
        }

        return fmt::to_string(msg);
    }

    int node_count() const noexcept { return popcount(bitmap); }

    // Empty bitmap_indexed_node, used to create subsequent nodes.
    // This saves creating a new node every time; as changes are
    // made to this node, copies are returned rather than modifying
    // this node itself. The edit is a null id, so it doesn't
    // match any actual thread
    inline static bitmap_indexed_node emptyBin{edit_type{}};

private:
    int index(uint32_t bit) const noexcept
    {
        return popcount(bitmap & (bit - 1));
    }

    auto ensureEditable(const edit_type& edit)
    {
        if (sameEdit(this->edit, edit)) {
            return this->template shared_from_base<bitmap_indexed_node>();
        }

        auto n = popcount(bitmap);
        int count;
        if (n >= 0) {
            // make room for next assoc
            count = n + 1;
        } else {
            // make room for first two assocs
            count = 2;
        }

        auto newArray = array_type(count);
        std::copy(array.cbegin(), array.cend(), newArray.begin());
        return std::make_shared<bitmap_indexed_node>(edit, bitmap,
                std::move(newArray));
    }

    shared_node createNode(uint32_t shift, value_type e1,
            hash_type key2hash, value_type e2) const
    {
        hash_type key1hash = Hash{}(e1.first);
        if (key1hash == key2hash) {
            typename hcn_node::array_type newArray{e1, e2};
            return std::make_shared<hcn_node>(
                    edit_type{}, key1hash, 2, std::move(newArray));
        }

        bool addedLeaf = false;
        return emptyBin
                .assoc(shift, key1hash, e1, addedLeaf)
                ->assoc(shift, key2hash, e2, addedLeaf);
    }

    shared_node createNode(const edit_type& edit,
            uint32_t shift, value_type e1,
            hash_type key2hash, value_type e2) const
    {
        hash_type key1hash = Hash{}(e1.first);
        if (key1hash == key2hash) {
            typename hcn_node::array_type newArray{
                    e1, e2};
            return std::make_shared<hcn_node>(
                    edit, key1hash, 2, std::move(newArray));
        }

        bool addedLeaf = false;
        return emptyBin
                .assoc(edit, shift, key1hash, e1, addedLeaf)
                ->assoc(edit, shift, key2hash, e2, addedLeaf);
    }

    edit_type edit;
    uint32_t bitmap = 0;
    array_type array;
};

template <class K, class T, class Hash>
class array_node final : public node<K, T, Hash>
{
    using Base = node<K, T, Hash>;
    using bin_node = bitmap_indexed_node<K, T, Hash>;

public:
    using value_type = typename Base::value_type;
    using shared_node = typename Base::shared_node;
    using entry_type = typename Base::entry_type;

    using array_type = std::array<shared_node, 32>;

    array_node(const edit_type& edit, int count, array_type&& array) :
        edit(edit),
        count(count),
        array(std::move(array))
    {}

    shared_node assoc(uint32_t shift, hash_type hash,
            const entry_type& newEntry, bool& addedLeaf)
    {
        auto idx = mask(hash, shift);
        auto node = array[idx];

        // add node with new value if not found
        if (!node) {
            return std::make_shared<array_node>(edit_type{}, count + 1,
                    setDup(array, idx, bin_node::emptyBin.assoc(shift + 5, hash, newEntry, addedLeaf)));
        }

        // otherwise, add the value to the node
        auto n = node->assoc(shift + 5, hash, newEntry, addedLeaf);
        if (n == node) {
            return this->shared_from_this();
        }

        return std::make_shared<array_node>(edit_type{}, count,
                setDup(array, idx, n));
    }

    shared_node without(uint32_t shift, hash_type hash, const K& key)
    {
        auto idx = mask(hash, shift);
        auto node = array[idx];
        if (!node) {
            return this->shared_from_this();
        }

        auto n = node->without(shift + 5, hash, key);
        if (n == node) {
            return this->shared_from_this();
        }
        if (!n) {
            if (count <= 8) {
                // shrink
                return pack(edit_type{}, int(idx));
            }

            return std::make_shared<array_node>(edit_type{}, count - 1,
                    setDup(array, idx, n));
        } else {
            return std::make_shared<array_node>(edit_type{}, count,
                    setDup(array, idx, n));
        }
    }

    std::optional<entry_type> find(uint32_t shift, hash_type hash,
            const K& key) const
    {
        auto idx = mask(hash, shift);
        auto node = array[idx];
        if (node) {
            return node->find(shift + 5, hash, key);
        }
        return std::nullopt;
    }

    shared_node assoc(const edit_type& edit, uint32_t shift, hash_type hash,
            const entry_type& newEntry, bool& addedLeaf)
    {
        auto idx = mask(hash, shift);
        auto node = array[idx];

        // add node with new value if not found
        if (!node) {
            auto editable = ensureEditable(edit);
            editable->array[idx] = bin_node::emptyBin.assoc(edit, shift + 5,
                    hash, newEntry, addedLeaf);
            editable->count++;
            return editable;
        }

        // otherwise, add the value to the node
        auto n = node->assoc(edit, shift + 5, hash, newEntry, addedLeaf);
        if (n == node) {
            return this->shared_from_this();
        }

        auto editable = ensureEditable(edit);
        editable->array[idx] = n;
        return editable;
    }

    shared_node without(const edit_type& edit, uint32_t shift, hash_type hash,
            const K& key, bool& removedLeaf)
    {
        auto idx = mask(hash, shift);
        auto node = array[idx];
        if (!node) {
            return this->shared_from_this();
        }
        auto n = node->without(edit, shift + 5, hash, key, removedLeaf);
        if (n == node) {
            return this->shared_from_this();
        }
        if (!n) {
            if (count <= 8) {
                // shrink
                return pack(edit, idx);
            }
            auto editable = ensureEditable(edit);
            editable->array[idx] = n;
            editable->count--;
            return editable;
        }
        auto editable = ensureEditable(edit);
        editable->array[idx] = n;
        return editable;
    }

    // todo: iterator(handler iterHandler) atom.SeqIterator

    std::string dump(int indent) const
    {
        fmt::memory_buffer msg;

        fmt::format_to(msg, "arn");
        for (std::size_t i = 0; i < array.size(); i++) {
            auto& node = array[i];
            for (int idt = 0; idt < indent; idt++) {
                fmt::format_to(msg, " ");
            }
            if (node) {
                fmt::format_to(msg, "{}: {}", i,
                        node->dump(indent + 1));
            } else {
                fmt::format_to(msg, "{}: null\n", i);
            }
        }

        return fmt::to_string(msg);
    }

    int node_count() const noexcept { return count; }

private:
    auto ensureEditable(const edit_type& edit)
    {
        if (sameEdit(this->edit, edit)) {
            return this->template shared_from_base<array_node>();
        }

        auto newArray{array};
        return std::make_shared<array_node>(edit, count, std::move(newArray));
    }

    auto pack(const edit_type& edit, int idx) const
    {
        typename bin_node::array_type newArray(count - 1);
        auto dest = newArray.begin() + 1;

        uint32_t bitmap = 0;
        for (int i = 0; i < idx; i++) {
            if (array[i]) {
                *dest++ = array[i];
                bitmap |= 1 << i;
            }
        }

        for (std::size_t i = idx + 1; i < array.size(); i++) {
            if (array[i]) {
                *dest++ = array[i];
                bitmap |= 1 << i;
            }
        }

        return std::make_shared<bin_node>(edit, bitmap, std::move(newArray));
    }

    edit_type edit;
    int count;
    array_type array;
};

template <class K, class T, class Hash>
class hash_collision_node final : public node<K, T, Hash>
{
    using Base = node<K, T, Hash>;
    using bin_node = bitmap_indexed_node<K, T, Hash>;

public:
    using value_type = typename Base::value_type;
    using shared_node = typename Base::shared_node;
    using entry_type = typename Base::entry_type;

    using array_type = std::vector<value_type>;

    hash_collision_node(const edit_type& edit, hash_type hash,
            typename array_type::size_type count, array_type newArray) :
        edit(edit),
        hash(hash),
        count(count),
        array(std::move(newArray))
    {}

    shared_node assoc(uint32_t shift, hash_type hash,
            const entry_type& newEntry, bool& addedLeaf)
    {
        // check the hash; if same, we can add it to a hcn
        if (this->hash == hash) {
            auto entry = std::get<value_type>(newEntry);
            auto idx = indexof(entry.first);

            // if we found the key, replace its val
            if (idx != -1) {
                if (array[idx].second == entry.second) {
                    return this->shared_from_this();
                }

                auto dup{array};
                dup[idx].second = entry.second;
                return std::make_shared<hash_collision_node>(edit_type{}, hash,
                        count, std::move(dup));
            }

            addedLeaf = true;

            auto newArray = array_type(count + 1);
            std::copy(array.cbegin(), array.cend(), newArray.begin());
            newArray[count] = entry;
            return std::make_shared<hash_collision_node>(edit, hash,
                    count + 1, std::move(newArray));
        }

        // nest it in a bitmap node
        typename bin_node::array_type selfArray{this->shared_from_this()};
        auto bin = std::make_shared<bin_node>(edit_type{},
                bitpos(this->hash, shift), std::move(selfArray));
        return bin->assoc(shift, hash, newEntry, addedLeaf);
    }

    shared_node without(uint32_t shift, hash_type hash, const K& key)
    {
        auto idx = indexof(key);
        if (idx == -1) {
            return this->shared_from_this();
        } else if (count == 1) {
            return {};
        } else {
            array_type newArray(count - 1);
            auto abegin = array.cbegin();
            std::copy_n(abegin, idx, newArray.begin());
            std::copy(abegin + idx + 1, array.cend(), newArray.begin() + idx);

            return std::make_shared<hash_collision_node>(edit_type{}, hash,
                    count - 1, std::move(newArray));
        }
    }

    std::optional<entry_type> find(uint32_t shift, hash_type hash,
            const K& key) const
    {
        auto idx = indexof(key);
        if (idx != -1) {
            return array[idx];
        }
        return std::nullopt;
    }

    shared_node assoc(const edit_type& edit, uint32_t shift, hash_type hash,
            const entry_type& newEntry, bool& addedLeaf)
    {
        // check the hash; if same, we can add it to a hcn
        if (this->hash == hash) {
            auto entry = std::get<value_type>(newEntry);
            auto idx = indexof(entry.first);

            // if we found the key, replace its val
            if (idx != -1) {
                if (array[idx].second == entry.second) {
                    return this->shared_from_this();
                }
                auto editable = ensureEditable(edit);
                editable->array[idx].second = entry.second;
                return editable;
            }

            addedLeaf = true;
            auto editable = ensureEditable(edit);
            if (array.size() > count) {
                editable->array[count] = entry;
            } else {
                editable->array.push_back(entry);
            }
            editable->count++;
            return editable;
        }

        // nest it in a bitmap node with an extra space
        typename bin_node::array_type selfArray{
                this->shared_from_this(), {}};

        auto bin = std::make_shared<bin_node>(edit, bitpos(this->hash, shift),
                std::move(selfArray));
        return bin->assoc(edit, shift, hash, newEntry, addedLeaf);
    }

    shared_node without(const edit_type& edit, uint32_t shift, hash_type hash,
            const K& key, bool& removedLeaf)
    {
        auto idx = indexof(key);
        if (idx == -1) {
            return this->shared_from_this();
        }

        removedLeaf = true;
        if (count == 1) {
            return {};
        }

        auto editable = ensureEditable(edit);
        editable->array[idx] = editable->array.back();
        editable->array.pop_back();
        editable->count--;
        return editable;
    }

    // todo: iterator(handler iterHandler) atom.SeqIterator

    std::string dump(int indent) const
    {
        fmt::memory_buffer msg;

        fmt::format_to(msg, "hcn\n");
        for (std::size_t i = 0; i < array.size(); i++) {
            auto& entry = array[i];
            for (int idt = 0; idt < indent; idt++) {
                fmt::format_to(msg, " ");
            }
            fmt::format_to(msg, "{}: {}->{}\n", i, entry.first, entry.second);
        }

        return fmt::to_string(msg);
    }

    int node_count() const noexcept { return count; }

private:
    int indexof(K key) const
    {
        for (decltype(count) i = 0; i < count; i++) {
            if (key == array[i].first) {
                return i;
            }
        }
        return -1;
    }

    auto ensureEditable(const edit_type& edit)
    {
        if (sameEdit(this->edit, edit)) {
            return this->template shared_from_base<hash_collision_node>();
        }

        auto newArray{array};
        return std::make_shared<hash_collision_node>(edit, hash,
                count, std::move(newArray));
    }

    edit_type edit;
    hash_type hash;
    typename array_type::size_type count;
    array_type array;
};

} // namespace pdata::detail

#endif // PDATA_MAP_DETAIL_H
