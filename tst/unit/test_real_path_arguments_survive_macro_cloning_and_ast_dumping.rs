//@ compile-flags: -Zdump-ast
//@ output-contains: _1_ast.rs WIDTH
#![feature(associated_const_equality)]
trait Layout { type Byte; const WIDTH: usize; }
struct Header;
impl Layout for Header { type Byte = u8; const WIDTH: usize = 3; }
#[derive(Clone, Copy)]
struct Bytes<'a, T: Copy, const N: usize> { data: &'a [T; N] }
macro_rules! define { ($item:item) => { $item }; }
define! {
    fn sum<L: Layout<Byte = u8, WIDTH = { 3 }>>(bytes: Bytes<'_, L::Byte, 3>) -> u8
    where L: Layout<Byte: Copy>
    {
        bytes.data[0] + bytes.data[2]
    }
}
fn main() {
    let data = [2u8, 3, 5];
    let bytes = Bytes { data: &data };
    assert_eq!(sum::<Header>(bytes.clone()), 7);
}
