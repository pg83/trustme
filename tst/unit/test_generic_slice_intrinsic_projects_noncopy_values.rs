//@ compile-variant: -O
#![feature(core_intrinsics)]
fn selected<T>(values: &[T], index: usize) -> &T {
    unsafe { core::intrinsics::slice_get_unchecked(values, index) }
}
fn main() {
    let values = [String::from("first"), String::from("second")];
    assert_eq!(selected(&values, 1), "second");
    assert_eq!(selected(&values, 0), "first");
    assert_eq!(values[1], "second");
}
