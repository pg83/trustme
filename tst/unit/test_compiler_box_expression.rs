#![feature(rustc_attrs, stmt_expr_attributes)]
#![allow(internal_features)]
use std::cell::Cell;
fn boxed<T>(value: T) -> Box<T> {
    // Exercise trustme's expression lowering; the allocation semantics match Box::new.
    #[cfg_attr(rust_compiler = "trustme", rustc_box)]
    Box::new(value)
}
struct Item<'a> { value: u8, drops: &'a Cell<u8> }
impl Drop for Item<'_> {
    fn drop(&mut self) { self.drops.set(self.drops.get() + 1); }
}
fn main() {
    let drops = Cell::new(0);
    let item = boxed(Item { value: 37, drops: &drops });
    assert_eq!(item.value, 37);
    assert_eq!(drops.get(), 0);
    drop(item);
    assert_eq!(drops.get(), 1);
    let bytes = boxed([23u8; 513]);
    assert_eq!(bytes[512], 23);
    let zero = boxed(());
    assert_eq!(*zero, ());
}
