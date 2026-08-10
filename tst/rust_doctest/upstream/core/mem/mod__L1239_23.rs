// Extracted from library/core/src/mem/mod.rs:1239
#![allow(unused)]
#![feature(sized_type_properties)]
fn main() {
    use core::mem::SizedTypeProperties;

    fn do_something_with<T>() {
        if T::IS_ZST {
            // ... special approach ...
        } else {
            // ... the normal thing ...
        }
    }

    struct MyUnit;
    assert!(MyUnit::IS_ZST);

    // For negative checks, consider using UFCS to emphasize the negation
    assert!(!<i32>::IS_ZST);
    // As it can sometimes hide in the type otherwise
    assert!(!String::IS_ZST);
}
