//@ compile-fail: functional record updates are not allowed in destructuring assignments
struct Pair { left: u32, right: u32 }
fn main() {
    let mut output = 0;
    let base = Pair { left: 1, right: 2 };
    Pair { left: output, ..base } = Pair { left: 17, right: 23 };
}
