fn main() {
    let mut value = 0u32;
    macro_rules! amount { () => { 17u32 }; }
    (|target: &mut u32| *target += amount!())(&mut value);
    let pointer = &mut value;
    macro_rules! addend { () => { 23u32 }; }
    *pointer += addend!();
    assert_eq!(value, 40);
}
