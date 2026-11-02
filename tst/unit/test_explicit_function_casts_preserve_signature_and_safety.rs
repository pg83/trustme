//@ compile-variant: -O
fn safe(value: u16) -> u16 { value + 3 }
unsafe fn unchecked(value: u16) -> u16 { value * 2 }

fn main() {
    let pointer = safe as fn(u16) -> u16;
    let widened = pointer as unsafe fn(u16) -> u16;
    let unsafe_pointer = unchecked as unsafe fn(u16) -> u16;
    let closure = (|value: u16| value + 7) as fn(u16) -> u16;
    assert_eq!(pointer(19), 22);
    assert_eq!(unsafe { widened(19) }, 22);
    assert_eq!(unsafe { unsafe_pointer(19) }, 38);
    assert_eq!(closure(19), 26);
    assert_eq!(safe as *const () as usize, pointer as usize);
    assert_eq!(unsafe_pointer as *const () as usize, unchecked as usize);
}
