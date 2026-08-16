#pragma once

/**
 * Vector-backed map that supports range lookups using different keys
 */

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

template <typename K, typename V, typename Cmp = std::less<K>>
class RangeVecMap {
public:
    typedef std::pair<K, V> itemT;

private:
    typedef std::vector<::std::unique_ptr<itemT>> innerT;
    innerT data_;
    Cmp cmp;

public:
    RangeVecMap() {
    }

    class iterator {
        friend class RangeVecMap<K, V, Cmp>;
        typename innerT::iterator inner;

        iterator(typename innerT::iterator i)
            : inner(i)
        {
        }

    public:
        itemT& operator*() {
            return **inner;
        }

        itemT* operator->() {
            return &**inner;
        }

        iterator& operator++() {
            ++inner;
            return *this;
        }

        iterator operator+(size_t i) const {
            return iterator(inner + i);
        }

        bool operator==(const iterator& x) const {
            return inner == x.inner;
        }

        bool operator!=(const iterator& x) const {
            return inner != x.inner;
        }

        ptrdiff_t operator-(const iterator& x) const {
            return inner - x.inner;
        }
    };

    class const_iterator {
        friend class RangeVecMap<K, V, Cmp>;
        typename innerT::const_iterator inner;

        const_iterator(typename innerT::const_iterator i)
            : inner(i)
        {
        }

    public:
        const itemT& operator*() {
            return **inner;
        }

        const itemT* operator->() {
            return &**inner;
        }

        const_iterator& operator++() {
            ++inner;
            return *this;
        }

        const_iterator operator+(size_t i) const {
            return const_iterator(inner + i);
        }

        bool operator==(const const_iterator& x) const {
            return inner == x.inner;
        }

        bool operator!=(const const_iterator& x) const {
            return inner != x.inner;
        }

        ptrdiff_t operator-(const const_iterator& x) const {
            return inner - x.inner;
        }
    };

    size_t size() const {
        return data_.size();
    }

    iterator begin() {
        return iterator(data_.begin());
    }

    const_iterator begin() const {
        return const_iterator(data_.begin());
    }

    iterator end() {
        return iterator(data_.end());
    }

    const_iterator end() const {
        return const_iterator(data_.end());
    }

    template <typename K2>
    iterator lower_bound(const K2& k) {
        return iterator(std::lower_bound(data_.begin(), data_.end(), k, [&](const ::std::unique_ptr<itemT>& kv, const K2& k) {
            return cmp(kv->first, k);
        }));
    }

    template <typename K2>
    iterator upper_bound(const K2& k) {
        return iterator(std::upper_bound(data_.begin(), data_.end(), k, [&](const K2& k, const ::std::unique_ptr<itemT>& kv) {
            return cmp(k, kv->first);
        }));
    }

    template <typename K2>
    std::pair<iterator, iterator> equal_range(const K2& k) {
        return std::make_pair(lower_bound(k), upper_bound(k));
    }

    /// Lower bound: First item in the map not less than the provided key (equal, or first after)
    template <typename K2>
    const_iterator lower_bound(const K2& k) const {
        return const_iterator(std::lower_bound(data_.begin(), data_.end(), k, [&](const ::std::unique_ptr<itemT>& kv, const K2& k) {
            return cmp(kv->first, k);
        }));
    }

    /// Upper bound: First item in the map after the provided key
    template <typename K2>
    const_iterator upper_bound(const K2& k) const {
        return const_iterator(std::upper_bound(data_.begin(), data_.end(), k, [&](const K2& k, const ::std::unique_ptr<itemT>& kv) {
            return cmp(k, kv->first);
        }));
    }

    /// Iterator pair of first and after-last items equal to the given key
    template <typename K2>
    std::pair<const_iterator, const_iterator> equal_range(const K2& k) const {
        return std::make_pair(lower_bound(k), upper_bound(k));
    }

    template <typename K2>
    iterator find(const K2& k) {
        auto v = equal_range(k);
        if (v.first == v.second) {
            return end();
        }
        return v.first;
    }

    template <typename K2>
    const_iterator find(const K2& k) const {
        auto v = equal_range(k);
        if (v.first == v.second) {
            return end();
        }
        return v.first;
    }

    std::pair<iterator, bool> insert(itemT kv) {
        auto its = this->equal_range(kv.first);
        if (its.first == its.second) {
            size_t i = its.first.inner - data_.begin();
            data_.insert(its.first.inner, std::make_unique<itemT>(std::move(kv)));
            return std::make_pair(iterator(data_.begin() + i), true);
        } else {
            assert(its.first + 1 == its.second);
            return std::make_pair(its.first, false);
        }
    }

    V& operator[](K k) {
        auto its = equal_range(k);
        if (its.first == its.second) {
            size_t i = its.first.inner - data_.begin();
            data_.insert(its.first.inner, std::make_unique<itemT>(std::make_pair(std::move(k), V())));
            return data_[i]->second;
        } else {
            assert(its.first.inner + 1 == its.second.inner);
            return its.first->second;
        }
    }

    void clear() {
        data_.clear();
    }
};
