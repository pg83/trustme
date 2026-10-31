struct Triple(u8, u8, u8);
struct Output { left: u8 }
fn main() {
    let mut output = Output { left: 0 };
    let mut right = 0;
    let pointer = &mut right;
    Triple(output.left, _, *pointer) = Triple(17, 23, 31);
    assert_eq!((output.left, right), (17, 31));
}
