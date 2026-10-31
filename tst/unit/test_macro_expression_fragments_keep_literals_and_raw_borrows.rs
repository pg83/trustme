//@ edition: 2024
macro_rules! keep { ($value:expr) => { $value }; }
fn main() {
    let character = keep!('q');
    assert_eq!(character as u32, 113);
    assert_eq!(keep!(b"az"), &[97, 122]);
    assert_eq!(keep!("hello"), "hello");
    assert_eq!(keep!(c"hi").to_bytes(), b"hi");
    assert_eq!(keep!(1.5f64), 1.5);
    assert_eq!(keep!(-17i32), -17);
    assert!(keep!(!false));
    assert_eq!(**keep!(&&13u32), 13);
    let mut value = 23u32;
    let shared = keep!(&raw const value);
    let unique = keep!(&raw mut value);
    keep!(unsafe { *unique = 37 });
    assert_eq!(keep!(unsafe { *shared }), 37);
    assert_eq!(keep!(const { 41u32 }), 41);
    assert_eq!(keep!(value as u64), 37);
}
