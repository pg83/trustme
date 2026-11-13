//@ aux-build: associated_callable_metadata.rs
extern crate associated_callable_metadata as exported;

use exported::Source;

type Operation = <() as Source>::Operation;

trait Layout {
    const WIDTH: usize;
}

impl<T> Layout for (T, Operation) {
    const WIDTH: usize = core::mem::size_of::<Operation>();
}

const WIDTH: usize = <(u8, Operation) as Layout>::WIDTH;

fn main() {
    let operation = <() as Source>::operation();
    let copy = operation;
    assert_eq!(operation(31), 38);
    assert_eq!(copy(41), 48);
    assert_eq!(WIDTH, 0);
    let _: [u8; WIDTH] = [];
}
