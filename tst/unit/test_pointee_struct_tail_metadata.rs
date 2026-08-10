#![feature(ptr_metadata)]

use std::fmt::Debug;
use std::ptr::{self, DynMetadata, Pointee};

struct Pair<A, B: ?Sized>(A, B);
struct Tail<T: ?Sized>(T);
struct Outer<T: ?Sized>(u8, Tail<T>);

fn metadata<T: ?Sized>(value: *const T) -> <T as Pointee>::Metadata {
    ptr::metadata(value)
}

fn same_metadata<T: ?Sized>()
where
    Outer<T>: Pointee<Metadata = <T as Pointee>::Metadata>,
{
}

fn main() {
    same_metadata::<[u8]>();
    same_metadata::<dyn Debug>();

    let value = Pair(true, [1_u8, 2, 3]);
    let dst: &Pair<bool, [u8]> = &value;
    assert_eq!(metadata(dst), 3);

    let nested = Outer(0, Tail([1_u8, 2, 3]));
    let nested_dst: &Outer<[u8]> = &nested;
    assert_eq!(metadata(nested_dst), 3);

    let object: &Pair<bool, dyn Debug> = &Pair(true, 7_u32);
    let _: DynMetadata<dyn Debug> = metadata(object);
}
