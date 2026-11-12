//@ compile-fail: Type mismatch
#![feature(generic_assert)]

struct Flag(bool);
impl std::ops::Not for Flag {
    type Output = bool;
    fn not(self) -> bool { !self.0 }
}

fn main() {
    assert!(Flag { 0: let inner = true });
}
