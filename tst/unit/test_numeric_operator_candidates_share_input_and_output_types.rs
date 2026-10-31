use std::ops::Add;
struct Lift<T>(T);
impl Add<i32> for Lift<i32> { type Output = i32; fn add(self, rhs: i32) -> i32 { self.0 + rhs + 1 } }
impl Add<i64> for Lift<i64> { type Output = i64; fn add(self, rhs: i64) -> i64 { self.0 + rhs + 100 } }
fn main() {
    let value = Lift(3) + 4;
    assert_eq!(value, 8);
    assert_eq!(std::mem::size_of_val(&value), 4);
}
