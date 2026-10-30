trait Read<T, const N: usize> { fn read(&self) -> [T; N]; }
impl Read<u16, 2> for u8 { fn read(&self) -> [u16; 2] { [*self as u16, *self as u16 + 1] } }
fn main() {
    let object: &(dyn Read<_, 2> + Send + Sync) = &7u8;
    let output: [u16; 2] = object.read();
    assert_eq!(output, [7, 8]);
}
