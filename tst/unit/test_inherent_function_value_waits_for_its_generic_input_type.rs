struct Cell<T>(T);
impl<T: Copy> Cell<T> {
    fn make(value: T) -> Self { Cell(value) }
    fn read(&self) -> T { self.0 }
}

fn main() {
    let make: fn(u16) -> Cell<u16> = Cell::<_>::make;
    let read: fn(&Cell<u16>) -> u16 = Cell::<_>::read;
    let value = make(42);
    assert_eq!(read(&value), 42);
}
