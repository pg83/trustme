#![feature(core_intrinsics)]
#[derive(Clone, Copy, Debug, PartialEq)]
struct Pair { left: u64, right: u64 }
fn main() {
    let mut scalar = 0u64;
    let mut pair = Pair { left: 0, right: 0 };
    let mut unit = ();
    unsafe {
        core::intrinsics::nontemporal_store(&mut scalar, 13);
        core::intrinsics::nontemporal_store(&mut pair, Pair { left: 17, right: 23 });
        core::intrinsics::nontemporal_store(&mut unit, ());
    }
    assert_eq!(scalar, 13);
    assert_eq!(pair, Pair { left: 17, right: 23 });
}
