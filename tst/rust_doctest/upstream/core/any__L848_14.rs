// Extracted from library/core/src/any.rs:848
#![allow(unused)]
fn main() {
    assert_eq!(
        std::any::type_name::<Option<String>>(),
        "core::option::Option<alloc::string::String>",
    );
}
