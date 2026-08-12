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

        ::std::string mStr;

    public:
        path();

        path(const ::std::string& s);

        path(const char* s);

        bool isValid() const {
            return mStr != "";
        }

        bool is_absolute() const {
            return mStr != "" && mStr[0] == '/';
        }

        bool operator==(const path& p) const {
            return mStr == p.mStr;
        }

        bool operator!=(const path& p) const {
            return mStr != p.mStr;
        }

        path& operator/=(const path& p);

        path& operator/=(const char* o);

        path& operator/=(::std::string_view o);

        path operator/(const path& p) const;

        /// Append a relative path
        path operator/(const char* o) const;

        /// Add an arbitary string to the final component
        path operator+(const char* o) const;

        bool popComponent();

        path parent() const;

        path to_absolute() const;

        ::std::string basename() const;

        const ::std::string& str() const {
            return mStr;
        }

        operator ::std::string() const {
            return mStr;
        }

        class ComponentsIter {
            const path& p;
            size_t pos;
            size_t end;

            friend class path;

            ComponentsIter(const path& p, size_t i);

        public:
            ::std::string_view operator*() const {
                return ::std::string_view(p.mStr.c_str() + pos, end - pos);
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
            return ComponentsIter(*this, mStr.size());
        }

        path normalise() const;

        //void normalise_in_place();

        friend ::std::ostream& operator<<(::std::ostream& os, const path& p) {
            return os << p.mStr;
        }
    };

}
