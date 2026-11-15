trait Family {
    type Item<T>;
}

struct Bytes;

impl Family for Bytes {
    type Item<T> = u8;
}

fn require<F: Family<Item<T> = u8>, T>(value: u8) -> u8 {
    value
}

fn through_array<const N: usize>(value: u8) -> u8 {
    require::<Bytes, [u8; N]>(value)
}

fn main() {
    assert_eq!(through_array::<3>(41), 41);
}
