//@ run-pass
// A `#[repr(C, packed)]` struct puts a reference wherever its fields fall, so
// the relocation for it sits at whatever offset that is -- not necessarily a
// multiple of the pointer size. Constant evaluation moves those bytes around
// as bytes, and has to carry the relocation with them.

#[repr(C, packed)]
struct Packed {
    head: i32,
    reference: &'static i32,
    tail: [i32; 3],
}

#[repr(align(8))]
struct Aligned(Packed);

const SWAPPED: (i32, i32) = {
    let mut first = Aligned(Packed { head: 0, reference: &1, tail: [0; 3] });
    let mut second = Aligned(Packed { head: 0, reference: &666, tail: [0; 3] });

    type Bytes = [u8; core::mem::size_of::<Aligned>()];
    unsafe {
        core::ptr::swap(
            core::ptr::from_mut(&mut first).cast::<Bytes>(),
            core::ptr::from_mut(&mut second).cast::<Bytes>(),
        );
    }

    unsafe { (*first.0.reference, *second.0.reference) }
};

fn main() {
    assert_eq!(SWAPPED, (666, 1));
}
