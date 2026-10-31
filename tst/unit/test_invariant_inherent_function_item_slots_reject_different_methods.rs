//@ compile-fail: Type mismatch
struct Factory;
impl Factory {
    fn first<T>(value: T) -> T { value }
    fn second<T>(value: T) -> T { value }
}
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn main() {
    let mut function = Factory::first::<u32>;
    replace(&mut function, Factory::second::<u32>);
}
