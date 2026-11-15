#![feature(core_intrinsics)]
#![allow(internal_features)]

use core::intrinsics::{atomic_nand, AtomicOrdering};

fn main() {
    let mut value = 41_u32;
    let pointer = &mut value as *mut u32;
    let mut slot = pointer;

    let previous = unsafe {
        atomic_nand::<*mut u32, { AtomicOrdering::SeqCst }>(&mut slot, pointer)
    };

    assert_eq!(previous, pointer);
    assert_eq!(unsafe { *previous }, 41);
    assert_eq!(slot as usize, !(pointer as usize));
}
