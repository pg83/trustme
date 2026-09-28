//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// derive-where's tests put `#[derive_where(Clone, Copy)]` on a union, and the
// attribute hands the union on to its derive. Both an attribute macro and a
// derive are given the item as its tokens - `union`, generics, where clause,
// fields with their attributes and visibility - the same as a struct with named
// fields; the union case of that visitor was a TODO that aborted the compiler.
use proc_macro_item_passthrough::{echo_item, HelperForward};

#[echo_item]
#[repr(C)]
union Bits<T>
where
    T: Copy,
{
    pub word: u32,
    #[allow(dead_code)]
    raw: T,
}

#[derive(HelperForward)]
union Derived {
    small: u8,
    wide: u64,
}

fn main() {
    let bits = Bits::<f32> { word: 0x3f80_0000 };
    assert_eq!(unsafe { bits.raw }, 1.0);
    assert_eq!(Derived::helper_forward_seen(), 1);
    let derived = Derived { wide: 0 };
    assert_eq!(unsafe { derived.small }, 0);
}
