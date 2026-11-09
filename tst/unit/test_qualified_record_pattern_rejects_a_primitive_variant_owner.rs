//@ compile-fail: Expeted path in pattern binding
#![feature(more_qualified_paths)]

fn main() {
    match 181u8 {
        <u8>::Missing { .. } => {}
    }
}
