fn increment(value: u16) -> u16 { value + 1 }
fn double(value: u16) -> u16 { value * 2 }

fn main() {
    let values = [17u16, 29];
    let second = unsafe { values.as_ptr().add(1) };
    let mut pointer = values.as_ptr();
    let mut function: fn(u16) -> u16 = increment;
    let mut number = 0i32;
    unsafe {
        std::ptr::write_volatile(&mut pointer, second);
        let observed = std::ptr::read_volatile(&pointer);
        assert_eq!(observed, second);
        assert_eq!(*observed, 29);
        let pointer_to_pointer: *const *const u16 = &pointer;
        let slot = std::ptr::read_volatile(&pointer_to_pointer);
        assert_eq!(std::ptr::read_volatile(slot), second);
        std::ptr::write_volatile(&mut function, double as fn(u16) -> u16);
        let call = std::ptr::read_volatile(&function);
        assert_eq!(call(13), 26);
        std::ptr::write_volatile(&mut number, -37);
        assert_eq!(std::ptr::read_volatile(&number), -37);
    }
}
