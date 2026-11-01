//@ compile-fail: type annotations needed
struct Cell<T>(T);
impl<T: Copy> Cell<T> {
    fn make(value: T) -> Self { Cell(value) }
}
fn main() {
    let _make = Cell::<_>::make;
}
