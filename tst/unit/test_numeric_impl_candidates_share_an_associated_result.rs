trait Combine<A, B> {
    type Output;
    fn combine(a: A, b: B) -> Self::Output;
}
struct Sum;
impl Combine<i32, i32> for Sum {
    type Output = u64;
    fn combine(a: i32, b: i32) -> u64 { (a + b) as u64 }
}
impl Combine<i64, i64> for Sum {
    type Output = u64;
    fn combine(a: i64, b: i64) -> u64 { (a + b + 100) as u64 }
}
fn main() {
    let value: u64 = Sum::combine(11, 13);
    assert_eq!(value, 24);
}
