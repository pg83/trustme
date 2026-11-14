fn main() {
    assert!(match 0 {
        #[rustfmt::skip]
        0 => true,
        _ => false,
    });
}
