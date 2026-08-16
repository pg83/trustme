// `type_name` shows a type's path as written. It used the general type printer,
// which decorates a path with crate tags and a trailing comma inside the
// argument list, and an executable's crate reads as the placeholder `bin#`
// rather than the name the crate was given.
//
// Same shape as the upstream tests type/type-name-basic.rs and
// const-generics/const-generic-type_name.rs.
use std::any::type_name;

struct Foo<T> {
    x: T,
}

struct Pair<A, B>(A, B);

struct Sized<const N: usize>;

mod inner {
    pub struct Nested;
}

fn main() {
    // Primitives have no path at all.
    assert_eq!(type_name::<isize>(), "isize");
    assert_eq!(type_name::<u8>(), "u8");
    assert_eq!(type_name::<bool>(), "bool");

    // A local type is named by this crate, not by the internal placeholder.
    assert_eq!(type_name::<Foo<usize>>(), "test_type_name_path::Foo<usize>");
    assert_eq!(
        type_name::<Pair<u8, Foo<i32>>>(),
        "test_type_name_path::Pair<u8, test_type_name_path::Foo<i32>>"
    );
    assert_eq!(type_name::<inner::Nested>(), "test_type_name_path::inner::Nested");

    // A const generic argument is part of the name.
    assert_eq!(type_name::<Sized<3>>(), "test_type_name_path::Sized<3>");

    // A library type keeps its own crate, without the version tag.
    assert_eq!(type_name::<Option<u32>>(), "core::option::Option<u32>");
}
