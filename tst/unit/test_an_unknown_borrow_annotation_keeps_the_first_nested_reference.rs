fn main() {
    let bytes = [31u8; 37];
    let nested = &bytes;
    let carried: &_ = &nested;
    assert_eq!(
        std::mem::size_of_val(carried),
        std::mem::size_of::<&[u8; 37]>()
    );
}
