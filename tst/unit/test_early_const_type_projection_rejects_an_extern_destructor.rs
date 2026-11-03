//@ compile-fail: Drop can only be implemented
#![feature(extern_types, const_trait_impl, const_destruct, inherent_associated_types)]
#![allow(incomplete_features)]

unsafe extern "C" { type Foreign; }

impl const Drop for Foreign {
    fn drop(&mut self) {}
}

const fn measure() -> usize {
    let mut storage = 0u8;
    let pointer = &mut storage as *mut u8 as *mut Foreign;
    unsafe { core::ptr::drop_in_place(pointer); }
    1
}

struct Source<const N: usize>;
impl Source<1> { type Output = u16; }

fn observe(_: <Source<{ measure() }>>::Output) {}

fn main() {}
