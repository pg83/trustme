//@ run-pass
// A field of a `#[repr(packed)]` struct sits below the alignment its own type
// asks for, and `Drop::drop` takes a `&mut Self` that may not. The field is
// dropped through an aligned copy instead. The packing reaches everything
// nested inside such a field, so a field of a field is dropped the same way.

use std::cell::Cell;
use std::mem;

struct Aligned<'a> {
    drops: &'a Cell<usize>,
}

impl<'a> Drop for Aligned<'a> {
    fn drop(&mut self) {
        assert_eq!(self as *const Aligned as usize % mem::align_of::<Aligned>(), 0);
        self.drops.set(self.drops.get() + 1);
    }
}

struct Wrapper<'a> {
    inner: Aligned<'a>,
    tail: Trivial,
}

struct Trivial;

impl Drop for Trivial {
    fn drop(&mut self) {}
}

#[repr(packed)]
struct Packed<'a>(u8, Aligned<'a>);

#[repr(packed)]
struct Nested<'a>(u8, Wrapper<'a>);

fn main() {
    let drops = &Cell::new(0);
    {
        let _p = Packed(0, Aligned { drops });
    }
    assert_eq!(drops.get(), 1);

    {
        let n = Nested(
            0,
            Wrapper {
                inner: Aligned { drops },
                tail: Trivial,
            },
        );
        // Move one field out, so what is left is dropped field by field.
        let _moved = n.1.tail;
    }
    assert_eq!(drops.get(), 2);
}
