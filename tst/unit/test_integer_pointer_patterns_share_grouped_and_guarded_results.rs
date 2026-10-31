const NIL: *mut u16 = 0usize as *mut u16;
const MARK: *mut u16 = 37usize as *mut u16;
const LAST: *mut u16 = usize::MAX as *mut u16;
fn grouped(pointer: *mut u16) -> u8 {
    match pointer { NIL => 7, MARK => 11, LAST => 13, _ => 17 }
}
fn guarded(pointer: *mut u16, allow: bool) -> u8 {
    match pointer { NIL if allow => 7, MARK if allow => 11, LAST if allow => 13, _ => 17 }
}
fn main() {
    for (address, expected) in [(0, 7), (37, 11), (usize::MAX, 13), (41, 17)] {
        let pointer = address as *mut u16;
        assert_eq!(grouped(pointer), expected);
        assert_eq!(guarded(pointer, true), expected);
        assert_eq!(guarded(pointer, false), 17);
    }
}
