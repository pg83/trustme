#pragma once

/*
 * mrustc common code
 * - by John Hodge (Mutabah)
 *
 * tools/common/path.h
 * - Generic representation of a filesystem path (HEADER)
 */

#include <cstring>
#include <string>
#include <string_view>
#include <stdexcept>

namespace helpers {

    /// Path helper class (because I don't want to include boost)
    class path {
        static const char SEP = '/';

        ::std::string m_str;

    public:
        path();

        path(const ::std::string& s);

        path(const char* s);

        bool is_valid() const {
            return m_str != "";
        }

        bool is_absolute() const {
            return m_str != "" && m_str[0] == '/';
        }

        bool operator==(const path& p) const {
            return m_str == p.m_str;
        }

        bool operator!=(const path& p) const {
            return m_str != p.m_str;
        }

        path& operator/=(const path& p);

        path& operator/=(const char* o);

        path& operator/=(::std::string_view o);

        path operator/(const path& p) const;

        /// Append a relative path
        path operator/(const char* o) const;

        /// Add an arbitary string to the final component
        path operator+(const char* o) const;

        bool pop_component();

        path parent() const;

        path to_absolute() const;

        ::std::string basename() const;

        const ::std::string& str() const {
            return m_str;
        }

        operator ::std::string() const {
            return m_str;
        }

        class ComponentsIter {
            const path& p;
            size_t pos;
            size_t end;

            friend class path;

            ComponentsIter(const path& p, size_t i);

        public:
            ::std::string_view operator*() const {
                return ::std::string_view(p.m_str.c_str() + pos, end - pos);
            }

            void operator++();

            bool operator!=(const ComponentsIter& x) const {
                return pos != x.pos;
            }
        };

        ComponentsIter begin() const {
            return ComponentsIter(*this, 0);
        }

        ComponentsIter end() const {
            return ComponentsIter(*this, m_str.size());
        }

        path normalise() const;

        //void normalise_in_place();

        friend ::std::ostream& operator<<(::std::ostream& os, const path& p) {
            return os << p.m_str;
        }
    };

}
