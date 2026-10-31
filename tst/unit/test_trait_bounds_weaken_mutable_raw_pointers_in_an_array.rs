trait Read { unsafe fn read(&self) -> u16; }
impl Read for *const u16 { unsafe fn read(&self) -> u16 { **self } }
fn total<R: Read>(values: &[R]) -> u16 { values.iter().map(|value| unsafe { value.read() }).sum() }
fn main() {
    let first = 12u16;
    let mut second = 15u16;
    let left = &first as *const u16;
    let right = &mut second as *mut u16;
    assert_eq!(total(&[left, right]), 27);
    second += 1;
    assert_eq!(second, 16);
}
