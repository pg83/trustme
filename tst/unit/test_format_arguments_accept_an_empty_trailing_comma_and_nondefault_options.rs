fn main() {
    let mut text = String::new();
    std::fmt::write(&mut text, format_args!("tail",)).unwrap();
    assert_eq!(text, "tail");

    let formatted = format!("{0:#06x}/{0:0>4}", 27);
    assert_eq!(formatted, "0x001b/0027");
}
