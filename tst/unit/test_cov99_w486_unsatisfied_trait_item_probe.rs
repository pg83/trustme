//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: choose

trait Missing {}
trait Choice {
    fn choose() -> u8;
}
struct Slot<T>(T);
impl<T: Missing> Choice for Slot<T> {
    fn choose() -> u8 { 7 }
}

pub fn exercise() {
    let _ = Slot::<u8>::choose();
}
