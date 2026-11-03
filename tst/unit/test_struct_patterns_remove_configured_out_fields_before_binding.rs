struct Data { value: u32 }

fn main() {
    let Data {
        #[cfg(any())]
        nonexistent: missing,
        value,
    } = Data { value: 41 };
    assert_eq!(value, 41);
}
