//@ compile-variant: -O
#![feature(more_qualified_paths)]
struct Record { value: u16 }
trait Project { type Output; }
struct Identity;
impl Project for Identity { type Output = Record; }
fn main() {
    let record = <Identity as Project>::Output { value: 787 };
    let <Identity as Project>::Output { value } = record;
    assert_eq!(value, 787);
}
