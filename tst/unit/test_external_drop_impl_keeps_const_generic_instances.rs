//@ aux-build: external_drop_self_types.rs
//@ compile-variant: -O
use std::cell::Cell;
use external_drop_self_types::Resource;
fn main() {
    let count = Cell::new(0);
    drop(Resource::<3>(&count));
    drop(Resource::<7>(&count));
    assert_eq!(count.get(), 10);
}
