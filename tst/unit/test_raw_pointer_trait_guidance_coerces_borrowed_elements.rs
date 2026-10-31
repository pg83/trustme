trait Read { unsafe fn read(self) -> u16; }
impl Read for *const u16 { unsafe fn read(self) -> u16 { *self } }
fn sum<T: Read + Copy>(values: [T; 2]) -> u16 { unsafe { values[0].read() + values[1].read() } }
fn main() {
    let first = 17u16;
    let second = 31u16;
    let values = [&first as *const u16, &second];
    assert_eq!(sum(values), 48);
}
