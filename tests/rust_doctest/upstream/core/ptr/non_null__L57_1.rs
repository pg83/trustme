// Extracted from library/core/src/ptr/non_null.rs:57
#![allow(unused)]
fn main() {
    use std::ptr::NonNull;

    assert_eq!(size_of::<NonNull<i16>>(), size_of::<Option<NonNull<i16>>>());
    assert_eq!(align_of::<NonNull<i16>>(), align_of::<Option<NonNull<i16>>>());

    assert_eq!(size_of::<NonNull<str>>(), size_of::<Option<NonNull<str>>>());
    assert_eq!(align_of::<NonNull<str>>(), align_of::<Option<NonNull<str>>>());
}
