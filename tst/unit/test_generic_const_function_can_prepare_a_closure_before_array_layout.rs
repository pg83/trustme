const fn width<const N: usize>() -> usize {
    let _callback = || { let value = N; value };
    N
}

fn main() {
    let bytes = [9u8; width::<3>()];
    assert_eq!(bytes.len(), 3);
    assert_eq!(bytes[2], 9);
}
