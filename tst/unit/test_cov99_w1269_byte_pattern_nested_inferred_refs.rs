fn main() {
    let classify = |value| {
        let matched = match &&value {
            b"hi" => true,
            _ => false,
        };
        let _: [u8; 2] = value;
        matched
    };

    assert!(classify(*b"hi"));
    assert!(!classify(*b"no"));
}
