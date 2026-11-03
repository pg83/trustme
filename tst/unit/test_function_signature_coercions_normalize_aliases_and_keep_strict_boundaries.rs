//@ reject-flags: --edition=2021 --cfg bad_return => Type mismatch || mismatched types
//@ reject-flags: --edition=2021 --cfg bad_argument => Type mismatch || mismatched types
//@ reject-flags: --edition=2021 --cfg bad_unsafe => Type mismatch || mismatched types
//@ reject-flags: --edition=2021 --cfg bad_abi => Type mismatch || mismatched types
#![feature(inherent_associated_types)]

struct Signature;
impl Signature { type Input = u32; type Output = u64; }
fn lift(value: Signature::Input) -> Signature::Output { value as u64 + 1 }
unsafe fn unsafe_lift(value: Signature::Input) -> Signature::Output { lift(value) }
extern "C" fn foreign_lift(value: Signature::Input) -> Signature::Output { lift(value) }

#[cfg(bad_return)]
fn invalid() { let _: fn(u32) -> u32 = lift; }
#[cfg(bad_argument)]
fn invalid() { let _: fn(u64) -> u64 = lift; }
#[cfg(bad_unsafe)]
fn invalid() { let _: fn(u32) -> u64 = unsafe_lift; }
#[cfg(bad_abi)]
fn invalid() { let _: fn(u32) -> u64 = foreign_lift; }

fn main() {
    let ordinary: fn(u32) -> u64 = lift;
    let weakened: unsafe fn(u32) -> u64 = lift;
    let unsafe_pointer: unsafe fn(u32) -> u64 = unsafe_lift;
    let foreign: extern "C" fn(u32) -> u64 = foreign_lift;
    assert_eq!(ordinary(17), 18);
    assert_eq!(unsafe { weakened(23) }, 24);
    assert_eq!(unsafe { unsafe_pointer(29) }, 30);
    assert_eq!(foreign(31), 32);
    assert_eq!((lift as fn(u32) -> u64)(41), 42);
}
