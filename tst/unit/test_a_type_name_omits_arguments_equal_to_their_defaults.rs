// axum's path deserializer reports `std::any::type_name::<Vec<u32>>()` for a
// nested sequence and its tests expect "alloc::vec::Vec<u32>". Upstream prints
// a path's generic arguments without the trailing ones equal to their
// defaults (`own_args_no_defaults`), a default read with the arguments before
// it substituted: `Vec<u32>` is `Vec<u32, Global>` and prints as `Vec<u32>`,
// while a non-default allocator stays. The name printed every argument.
use std::any::type_name;
use std::collections::HashMap;

#[allow(dead_code)]
struct Pair<T, U = Vec<T>>(T, U);

fn main() {
    assert_eq!(type_name::<Vec<u32>>(), "alloc::vec::Vec<u32>");
    assert_eq!(type_name::<Vec<Vec<String>>>(), "alloc::vec::Vec<alloc::vec::Vec<alloc::string::String>>");
    assert_eq!(type_name::<Box<u8>>(), "alloc::boxed::Box<u8>");
    assert_eq!(type_name::<HashMap<u32, u8>>(), "std::collections::hash::map::HashMap<u32, u8>");
    assert!(type_name::<Pair<u8>>().ends_with("::Pair<u8>"));
    assert!(type_name::<Pair<u8, Vec<u16>>>().ends_with("::Pair<u8, alloc::vec::Vec<u16>>"));
    assert_eq!(type_name::<Option<Vec<u8>>>(), "core::option::Option<alloc::vec::Vec<u8>>");
}
