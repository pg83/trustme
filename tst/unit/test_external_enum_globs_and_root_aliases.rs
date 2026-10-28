//@ aux-build: external_glob_shapes.rs
use external_glob_shapes::Parcel::*;
use external_glob_shapes::Code::*;
use external_glob_shapes as dependency;
use crate as root;

#[macro_export]
macro_rules! make_tuple { ($value:expr) => { Tuple($value) }; }

fn read(value: external_glob_shapes::Parcel) -> u16 {
    match value { Empty => 0, Tuple(value) => value, Record { value } => value + 1 }
}

fn main() {
    assert_eq!(read(Empty), 0);
    assert_eq!(read(root::make_tuple!(7)), 7);
    assert_eq!(read(dependency::make_parcel!(11)), 12);
    assert_eq!(Low as i32 + High as i32, 13);
}
