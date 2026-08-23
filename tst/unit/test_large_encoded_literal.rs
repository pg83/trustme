static DATA: [u8; 32 * 1024 * 1024] = [42; 32 * 1024 * 1024];

#[inline(never)]
fn read(index: usize) -> u8 {
    DATA[index]
}

fn main() {
    assert_eq!(read(0), 42);
    assert_eq!(read(DATA.len() - 1), 42);
}
