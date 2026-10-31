trait Read { fn read(&self) -> u16; }
impl Read for &u16 { fn read(&self) -> u16 { **self } }
fn total<R: Read>(values: &[R]) -> u16 { values.iter().map(|value| value.read()).sum() }
fn main() {
    let first = 12;
    let mut second = 15;
    assert_eq!(total(&[&first, &mut second]), 27);
    second += 1;
    assert_eq!(second, 16);
}
