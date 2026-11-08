//@ compile-fail: Union constructors can only specify a single field
union Word { low: u8, full: u16 }
fn main() {
    let (low, full) = (0, 0);
    let _ = Word { low, full, .. };
}
