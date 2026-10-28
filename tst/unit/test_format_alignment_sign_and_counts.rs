fn main() {
    assert_eq!(format!("{:*<5}", "ab"), "ab***");
    assert_eq!(format!("{:*^6}", "ab"), "**ab**");
    assert_eq!(format!("{:*>5}", "ab"), "***ab");
    assert_eq!(format!("{:+}", 7), "+7");
    assert_eq!(format!("{:-}", 7), "7");
    assert_eq!(format!("{:#06x}", 42), "0x002a");
    assert_eq!(format!("{:+07.2}", 1.25), "+001.25");
    assert_eq!(format!("{value:>width$.precision$}", value = "abcdef", width = 5, precision = 3), "  abc");
    assert_eq!(format!("{:.*}", 2, "abcdef"), "ab");
    assert_eq!(format!("{0:^1$}", "é🦀", 4), " é🦀 ");
}
