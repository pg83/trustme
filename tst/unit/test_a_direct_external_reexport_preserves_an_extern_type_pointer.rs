//@ aux-build: typed_glob_exports.rs
fn main() {
    let pointer = std::ptr::null::<typed_glob_exports::nested::Opaque>();
    assert!(pointer.is_null());
    assert_eq!(std::mem::size_of_val(&pointer), std::mem::size_of::<usize>());
}
