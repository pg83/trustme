//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Clink-arg=-latomic
//@ input: emulated_i128_target.toml
#![feature(core_intrinsics)]
#![allow(internal_features)]

use core::intrinsics::{atomic_cxchg, AtomicOrdering};

#[repr(align(16))]
struct Slot(u128);

fn main() {
    const INITIAL: u128 = (1u128 << 111) | 191;
    const REPLACEMENT: u128 = (1u128 << 95) | 197;
    let mut slot = Slot(INITIAL);
    let pointer = &mut slot.0 as *mut u128;
    let changed = unsafe {
        atomic_cxchg::<u128, { AtomicOrdering::SeqCst }, { AtomicOrdering::Acquire }>(
            pointer, INITIAL, REPLACEMENT,
        )
    };
    assert_eq!(changed, (INITIAL, true));
    let unchanged = unsafe {
        atomic_cxchg::<u128, { AtomicOrdering::AcqRel }, { AtomicOrdering::Relaxed }>(
            pointer, REPLACEMENT ^ (1u128 << 95), 211,
        )
    };
    assert_eq!(unchanged, (REPLACEMENT, false));
    assert_eq!(slot.0, REPLACEMENT);
}
