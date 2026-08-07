fn main() {
    assert_eq!(format!("{:05}", -1), "-0001");
    assert_eq!(format!("{:+05}", -1), "-0001");
    assert_eq!(format!("{:#08x}", 10), "0x00000a");
}
