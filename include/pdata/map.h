#ifndef PDATA_MAP_H
#define PDATA_MAP_H

#include "fmt/format.h"
#include "pdata/map-detail.h"

#include <array>
#include <atomic>
#include <bit>
#include <functional>
#include <limits>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace pdata {

template <class K, class T>
class map_base : public std::enable_shared_from_this<map_base<K, T>>
{
public:
    virtual ~map_base() = default;

    virtual std::size_t size() const noexcept = 0;

    virtual std::optional<T> find(const K& key) const = 0;

    virtual std::string dump(int indent) const = 0;

protected:
    template <typename Derived>
    std::shared_ptr<Derived> shared_from_base()
    {
        return std::static_pointer_cast<Derived>(this->shared_from_this());
    }
};

template <class K, class T, class Hash>
class persistent_map;

template <class K, class T, class Hash = std::hash<K>>
class transient_map : public map_base<K, T>
{
    using node_type = detail::node<K, T, Hash>;
    using bin_node = detail::bitmap_indexed_node<K, T, Hash>;

public:
    transient_map() :
        edit(std::make_shared<std::thread::id>(std::this_thread::get_id()))
    {}

    std::size_t size() const noexcept { return count; }

    std::shared_ptr<transient_map> assoc(const K& key, T val)
    {
        // todo: check whether edit is invalid before reset here...
        // if so, transient used after persistent

        typename node_type::value_type entry{key, val};

        bool addedLeaf = false;

        // if we don't have a root, add one
        decltype(root) newroot{};
        if (!root) {
            newroot = bin_node::emptyBin
                              .assoc(edit, 0, Hash{}(entry.first), entry, addedLeaf);
        } else {
            newroot = root->assoc(edit, 0, Hash{}(entry.first), entry, addedLeaf);
        }

        if (newroot != root) {
            root = newroot;
        }

        if (addedLeaf) {
            count++;
        }

        return this->template shared_from_base<transient_map>();
    }

    std::shared_ptr<transient_map> without(const K& key)
    {
        // todo: check whether edit is invalid before reset here...
        // if so, transient used after persistent

        if (!root) {
            return this->template shared_from_base<transient_map>();
        }

        bool removedLeaf = false;
        auto newroot = root->without(edit, 0, Hash{}(key), key, removedLeaf);

        if (newroot != root) {
            root = newroot;
        }

        if (removedLeaf) {
            count--;
        }

        return this->template shared_from_base<transient_map>();
    }

    std::optional<T> find(const K& key) const
    {
        // todo: check whether edit is invalid before reset here...
        // if so, transient used after persistent

        if (root) {
            auto entry = root->find(0, Hash{}(key), key);
            if (entry) {
                return std::get<typename node_type::value_type>(*entry).second;
            }
        }

        return std::nullopt;
    }

    std::string dump(int indent) const
    {
        fmt::memory_buffer msg;
        fmt::writer writer(msg);

        writer.write("tmap\n");
        for (int idt = 0; idt < indent; idt++) {
            fmt::format_to(msg, " ");
        }
        if (root) {
            fmt::format_to(msg, "root: {}",
                    root->dump(indent + 1));
        } else {
            fmt::format_to(msg, "root: null\n");
        }

        return fmt::to_string(msg);
    }

    std::shared_ptr<persistent_map<K, T, Hash>> persistent()
    {
        // todo: check whether edit is invalid before reset here...
        // if so, transient used after persistent

        edit.reset();

        // struct to allow creation using make_shared and a private ctor
        struct pm_maker : public persistent_map<K, T, Hash>
        {
            pm_maker(int count, std::shared_ptr<node_type> root) :
                persistent_map<K, T, Hash>(count, root)
            {}
        };

        return std::make_shared<pm_maker>(count, root);
    }

private:
    friend class persistent_map<K, T, Hash>;

    transient_map(int count, std::shared_ptr<node_type> root) :
        edit(std::make_shared<std::thread::id>(std::this_thread::get_id())),
        count(count),
        root(root)
    {}

    std::shared_ptr<std::thread::id> edit;
    int count = 0;
    std::shared_ptr<node_type> root;
};

template <class K, class T, class Hash = std::hash<K>>
class persistent_map : public map_base<K, T>
{
    using node_type = detail::node<K, T, Hash>;
    using bin_node = detail::bitmap_indexed_node<K, T, Hash>;

public:
    persistent_map() = default;

    std::size_t size() const noexcept { return count; }

    // Returns a new persistent_map, adding or replacing a value in the map.
    std::shared_ptr<persistent_map> assoc(const K& key, T val)
    {
        typename node_type::value_type entry{key, val};

        bool addedLeaf = false;

        // if we don't have a root, make one from emptyBin, otherwise
        // add the new item to the root
        decltype(root) newroot{};
        if (!root) {
            newroot = bin_node::emptyBin
                              .assoc(0, Hash{}(entry.first), entry, addedLeaf);
        } else {
            newroot = root->assoc(0, Hash{}(entry.first), entry, addedLeaf);
        }

        if (newroot == root) {
            return this->template shared_from_base<persistent_map>();
        }

        auto cnt = count;
        if (addedLeaf) {
            cnt++;
        }

        // struct to allow creation using make_shared and a private ctor
        struct pm_maker : public persistent_map
        {
            pm_maker(int count, std::shared_ptr<node_type> root) :
                persistent_map(count, root)
            {}
        };

        return std::make_shared<pm_maker>(cnt, newroot);
    }

    std::shared_ptr<persistent_map> without(const K& key)
    {
        if (!root) {
            return this->template shared_from_base<persistent_map>();
        }

        // ignore removed flag, since we either do nothing or
        // create a new map with one less value
        auto newroot = root->without(0, Hash{}(key), key);

        if (newroot == root) {
            return this->template shared_from_base<persistent_map>();
        }

        // struct to allow creation using make_shared and a private ctor
        struct pm_maker : public persistent_map
        {
            pm_maker(int count, std::shared_ptr<node_type> root) :
                persistent_map(count, root)
            {}
        };

        return std::make_shared<pm_maker>(count - 1, newroot);
    }

    std::optional<T> find(const K& key) const
    {
        if (root) {
            auto entry = root->find(0, Hash{}(key), key);
            if (entry) {
                return std::get<typename node_type::value_type>(*entry).second;
            }
        }

        return std::nullopt;
    }

    std::string dump(int indent) const
    {
        fmt::memory_buffer msg;
        fmt::writer writer(msg);

        writer.write("pmap\n");
        for (int idt = 0; idt < indent; idt++) {
            fmt::format_to(msg, " ");
        }
        if (root) {
            fmt::format_to(msg, "root: {}",
                    root->dump(indent + 1));
        } else {
            fmt::format_to(msg, "root: null\n");
        }

        return fmt::to_string(msg);
    }

    std::shared_ptr<transient_map<K, T, Hash>> transient()
    {
        // struct to allow creation using make_shared and a private ctor
        struct tm_maker : public transient_map<K, T, Hash>
        {
            tm_maker(int count, std::shared_ptr<node_type> root) :
                transient_map<K, T, Hash>(count, root)
            {}
        };

        return std::make_shared<tm_maker>(count, root);
    }

private:
    friend class transient_map<K, T, Hash>;

    persistent_map(int count, std::shared_ptr<node_type> root) :
        count(count),
        root(root)
    {}

    int count = 0;
    std::shared_ptr<node_type> root;
};

} // namespace pdata

#endif // PDATA_MAP_H
