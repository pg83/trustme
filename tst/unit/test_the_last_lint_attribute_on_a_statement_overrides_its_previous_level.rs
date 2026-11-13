#![deny(unsafe_code)]

fn main() {
    let source = 47u16;

    #[deny(unsafe_code)]
    #[allow(unsafe_code)]
    let value = unsafe { *(&source as *const u16) };

    assert_eq!(value, 47);
}
