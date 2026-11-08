//@ compile-fail: union patterns must select exactly one field
union Word { low: u8, full: u16 }
fn main() {
    let (mut low, mut full) = (0, 0);
    Word { low, full } = Word { full: 17 };
}
