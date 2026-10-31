trait Read { fn read(&self) -> u16; }
impl Read for &u16 { fn read(&self) -> u16 { **self } }
fn sum<T: Read>(values: [T; 2]) -> u16 { values[0].read() + values[1].read() }
fn main() {
    let first = 13u16;
    let mut second = 29u16;
    let values = [&first, &mut second];
    assert_eq!(sum(values), 42);
    second += 1;
    assert_eq!(second, 30);
}
