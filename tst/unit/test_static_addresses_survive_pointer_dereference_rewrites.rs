static VALUE: u32 = 37;
static VALUES: [u16; 3] = [17, 23, 41];
const VALUE_ADDRESS: *const u32 = &VALUE;
const ARRAY_ADDRESS: *const u16 = VALUES.as_ptr();
const SECOND_ADDRESS: *const u16 = unsafe { ARRAY_ADDRESS.add(1) };

fn marker() -> u32 { 43 }
#[inline(always)]
fn invoke<F: Fn() -> u32>(call: F) -> u32 { call() }

fn main() {
    let call = marker as fn() -> u32;
    assert_eq!((&call)(), 43);
    assert_eq!(invoke(&marker), 43);
    assert_eq!(invoke(&&marker), 43);
    let value = VALUE_ADDRESS;
    let array = ARRAY_ADDRESS;
    let second = SECOND_ADDRESS;
    unsafe {
        assert_eq!(*value, 37);
        assert_eq!(*array, 17);
        assert_eq!(*second, 23);
    }
}
