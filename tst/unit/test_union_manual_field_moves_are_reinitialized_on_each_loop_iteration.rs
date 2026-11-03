//@ compile-variant: -O
use std::mem::ManuallyDrop;

union Slot { owned: ManuallyDrop<Box<u16>>, unused: () }

#[inline(never)]
fn collect(alternate: bool) -> u16 {
    let mut slot: Slot;
    let mut total = 0;
    for value in [3, 7, 11, 13] {
        slot = Slot { owned: ManuallyDrop::new(Box::new(value)) };
        if alternate && value % 2 == 1 {
            let owned = ManuallyDrop::into_inner(unsafe { slot.owned });
            total += *owned;
        } else {
            total += unsafe { **slot.owned };
            unsafe { ManuallyDrop::drop(&mut slot.owned); }
        }
    }
    total
}

fn main() {
    assert_eq!(collect(std::hint::black_box(true)), 34);
    assert_eq!(collect(std::hint::black_box(false)), 34);
}
