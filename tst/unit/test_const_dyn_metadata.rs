//@ run-pass
// The vtable a `DynMetadata` points at is an extern type, which carries no
// metadata of its own -- so the pointer to it is thin. Lowering a constant
// that holds one used to stop at the pointee having no metadata kind it knew.

#![feature(ptr_metadata)]

use std::fmt::Debug;
use std::ptr::DynMetadata;

const ARRAY_META: () = std::ptr::metadata::<[u16; 3]>(&[1, 2, 3]);
const SLICE_META: usize = std::ptr::metadata::<[u16]>(&[1, 2, 3]);
const DYN_META: DynMetadata<dyn Debug> = std::ptr::metadata::<dyn Debug>(&[0_u8; 42]);

fn main() {
    assert_eq!(ARRAY_META, ());
    assert_eq!(SLICE_META, 3);
    assert_eq!(DYN_META.size_of(), 42);
}
