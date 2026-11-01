fn main() {
    mod u16 { pub fn value() -> u32 { 89 } }
    use u16 as Local;
    assert_eq!(Local::value(), 89);
}
