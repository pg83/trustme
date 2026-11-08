//@ compile-fail: Use of #[macro_export] on non-macro
#![deny(unused_attributes)]
macro_rules! import_item {
    () => {
        #[macro_export]
        use std::mem::drop as ordinary_item;
    };
}
import_item!();
fn main() {}
