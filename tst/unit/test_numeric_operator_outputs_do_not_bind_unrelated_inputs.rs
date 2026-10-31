use std::ops::Add;
struct Lift;
impl Add<i32> for Lift { type Output = u16; fn add(self, rhs: i32) -> u16 { (rhs + 1) as u16 } }
impl Add<i64> for Lift { type Output = u32; fn add(self, rhs: i64) -> u32 { (rhs + 100) as u32 } }
fn main() {
    let value = Lift + 4;
    assert_eq!(value, 5u16);
    assert_eq!(std::mem::size_of_val(&value), 2);
}
