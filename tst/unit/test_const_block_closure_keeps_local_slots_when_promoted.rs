fn read<const N: u8>() -> u8 {
    let callback = &const { || { let value = N; value } };
    callback()
}

fn main() {
    assert_eq!(read::<7>(), 7);
    assert_eq!(read::<19>(), 19);
}
