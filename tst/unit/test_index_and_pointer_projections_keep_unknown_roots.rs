#[inline(never)]
fn update(values: &mut [u16; 3], index: usize) -> u16 {
    values[index] = 37;
    values[index]
}
#[inline(never)]
unsafe fn pointer(value: *mut u16) -> u16 {
    *value = 41;
    *value
}
fn main() {
    let index = std::hint::black_box(1usize);
    let mut values = [17, 23, 29];
    assert_eq!(update(&mut values, index), 37);
    assert_eq!(unsafe { pointer(&mut values[2]) }, 41);
    assert_eq!(values, [17, 37, 41]);
}
