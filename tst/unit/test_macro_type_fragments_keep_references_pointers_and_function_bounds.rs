macro_rules! typed {
    ($ty:ty, $value:expr) => {{ let value: $ty = $value; value }};
}
fn identity(value: &str) -> &str { value }
fn never() -> ! { panic!("not called") }
fn main() {
    let value = 31u32;
    let pair = typed!((&u32, [u8; 2]), (&value, [5, 7]));
    assert_eq!((*pair.0, pair.1[1]), (31, 7));
    let reference = &value;
    assert_eq!(**typed!(&&u32, &reference), 31);
    assert_eq!(typed!(&'static str, "word"), "word");
    assert_eq!(typed!(_, 37u32), 37);
    assert_eq!(unsafe { *typed!(*const u32, &value) }, 31);
    let mut count = 41u32;
    *typed!(&mut u32, &mut count) += 1;
    unsafe { *typed!(*mut u32, &mut count) += 1; }
    assert_eq!(count, 43);
    let identity = typed!(for<'a> fn(&'a str) -> &'a str, identity);
    assert_eq!(identity("lifetime"), "lifetime");
    let callback = typed!(&dyn Fn(u8) -> u8, &|n| n + 2);
    assert_eq!(callback(7), 9);
    let _never = typed!(fn() -> !, never);
}
