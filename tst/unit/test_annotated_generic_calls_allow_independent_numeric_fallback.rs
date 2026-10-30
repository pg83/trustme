fn unspecified<T>() -> T { loop {} }
fn main() {
    let integer = 17;
    let floating = 0.5;
    let character = 65 as char;
    let empty = [];
    let pointer = &empty as *const u16;
    if false { let _value: u16 = unspecified(); }
    assert_eq!(std::mem::size_of_val(&integer), 4);
    assert_eq!(std::mem::size_of_val(&floating), 8);
    assert_eq!(character, 'A');
    assert_eq!(pointer as usize, (&empty as *const [u16; 0]) as usize);
}
