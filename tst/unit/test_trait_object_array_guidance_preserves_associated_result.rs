trait Value { fn value(&self) -> u16; }
struct A(u16);
struct B(u16);
impl Value for A { fn value(&self) -> u16 { self.0 } }
impl Value for B { fn value(&self) -> u16 { self.0 + 10 } }
trait Read { type Output; fn read(&self) -> Self::Output; }
impl Read for &dyn Value { type Output = u16; fn read(&self) -> u16 { self.value() } }
fn read<R: Read<Output = u16>>(values: [R; 3]) -> u16 {
    values[0].read() + 2 * values[1].read() + 3 * values[2].read()
}
fn main() {
    let a = A(7);
    let b = B(11);
    let values = [&a as &dyn Value, &b, &a];
    assert_eq!(read(values), 70);
}
