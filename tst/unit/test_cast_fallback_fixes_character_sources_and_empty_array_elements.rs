fn main() {
    let first = 65 as char;
    let second = 90 as char;
    assert_eq!((first, second), ('A', 'Z'));
    let numbers = [19, 23];
    let number_pointer = &numbers as *const i32;
    assert_eq!(unsafe { *number_pointer.add(1) }, 23);
    let empty = [];
    let pointer = &empty as *const u16;
    assert_eq!(pointer as usize, (&empty as *const [u16; 0]) as usize);
}
