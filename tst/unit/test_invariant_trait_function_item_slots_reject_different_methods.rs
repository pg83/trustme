//@ compile-fail: Type mismatch
trait Factory {
    fn first<T>(value: T) -> T { value }
    fn second<T>(value: T) -> T { value }
}
impl Factory for u8 {}
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn main() {
    let mut function = <u8 as Factory>::first::<u32>;
    replace(&mut function, <u8 as Factory>::second::<u32>);
}
