//@ aux-build: inline_const_gat_metadata_indices.rs
extern crate inline_const_gat_metadata_indices as signals;

fn main() {
    let (short, first) = signals::first(29u16);
    let (wide, second) = signals::second(31u64);
    let (again, third) = signals::first(37u8);
    assert_eq!((short, wide, again), (29, 31, 37));
    assert_eq!((first.read(), second.read(), third.read()), (41, 73, 41));
}
