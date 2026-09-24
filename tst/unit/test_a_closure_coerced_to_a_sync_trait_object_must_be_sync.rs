// std's `lang_start` coerces `&move || ..` into `&(dyn Fn() -> i32 + Sync +
// RefUnwindSafe)`. The unsizing is selected at once and the object's auto
// traits are nested obligations of it (upstream `coerce_unsized` queues
// only `Unsize`/`CoerceUnsized`), decided once the closure's captures are
// known: a closure holding a `Cell` is not `Sync`.
//@ compile-fail: Failed to find an impl
use std::cell::Cell;

fn take(f: &(dyn Fn() -> i32 + Sync)) -> i32 {
    f()
}

fn main() {
    let n = 3;
    assert_eq!(take(&move || n), 3);
    let c = Cell::new(1);
    take(&move || c.get());
}
