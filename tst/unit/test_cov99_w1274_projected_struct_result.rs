//@ edition: 2021
//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

struct Record {
    value: u8,
}

trait Provides {
    type Item;
}

struct Factory;

impl Provides for Factory {
    type Item = Record;
}

type Projected = <Factory as Provides>::Item;

fn make<T: Provides<Item = Record>>() -> T::Item {
    Projected { value: 17 }
}

pub fn probe() -> u8 {
    make::<Factory>().value
}
