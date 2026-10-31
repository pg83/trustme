fn main() {
    macro_rules! selected { () => { 17u8 }; }
    let first = selected!();
    #[cfg(any())]
    macro_rules! selected { () => { 99u8 }; }
    macro_rules! selected { () => { 23u8 }; }
    [selected!(), first].iter().for_each(|value| assert!(*value == 17 || *value == 23));
    assert_eq!((first, selected!()), (17, 23));
}
