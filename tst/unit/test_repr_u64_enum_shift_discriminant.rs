#[repr(u64)]
#[derive(Copy, Clone)]
enum Shifted {
    One = 1 << 0,
    High = 1 << 40,
}

fn main() {
    assert_eq!(Shifted::One as u64, 1);
    assert_eq!(Shifted::High as u64, 1 << 40);

    let mut value = Shifted::High as u64;
    let bits: u32 = 4;
    value >>= bits;
    assert_eq!(value, 1 << 36);
}
