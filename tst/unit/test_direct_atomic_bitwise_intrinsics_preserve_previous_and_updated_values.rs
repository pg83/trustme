#![feature(core_intrinsics)]
use core::intrinsics::{atomic_and, atomic_xor, AtomicOrdering};

fn main() {
    let mut value = 0b11110000u32;
    unsafe {
        assert_eq!(atomic_and::<u32, { AtomicOrdering::Relaxed }>(&mut value, 0b00111100), 0b11110000);
        assert_eq!(atomic_xor::<u32, { AtomicOrdering::AcqRel }>(&mut value, 0b00001010), 0b00110000);
        assert_eq!(atomic_and::<u32, { AtomicOrdering::SeqCst }>(&mut value, 0b00001111), 0b00111010);
    }
    assert_eq!(value, 0b00001010);
}
