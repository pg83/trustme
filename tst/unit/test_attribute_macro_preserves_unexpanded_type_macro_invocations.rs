//@ proc-macro-aux-build: preserve_bound_types.rs
use preserve_bound_types::preserve;
macro_rules! component { () => { u16 }; }

#[preserve]
struct Record { first: component!(), pair: (component!(), u8) }

#[preserve]
fn identity(value: component!()) -> component!() { value }

fn main() {
    let record = Record { first: identity(11), pair: (13, 17) };
    assert_eq!((record.first, record.pair), (11, (13, 17)));
}
