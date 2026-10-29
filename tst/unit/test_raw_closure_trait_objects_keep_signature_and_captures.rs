fn main() {
    let identity = |value| value;
    let pointer = &identity as *const dyn Fn(u8) -> u8;
    assert_eq!(unsafe { (&*pointer)(23) }, 23);
    let default = || Default::default();
    let pointer = &default as *const dyn Fn() -> u16;
    assert_eq!(unsafe { (&*pointer)() }, 0);
    let seed = std::hint::black_box(17u16);
    let closure = |value: u8| seed + value as u16;
    let pointer = &closure as *const dyn Fn(u8) -> u16;
    assert_eq!(unsafe { (&*pointer)(3) }, 20);
    let mut sum = 5u32;
    let mut closure = |value: u8| { sum += value as u32; sum };
    let pointer = &mut closure as *mut dyn FnMut(u8) -> u32;
    assert_eq!(unsafe { (&mut *pointer)(7) }, 12);
    assert_eq!(unsafe { (&mut *pointer)(11) }, 23);
    let owned = String::from("tag");
    let closure = move |suffix: u8| { let mut text = owned; text.push(suffix as char); text };
    let pointer = Box::into_raw(Box::new(closure)) as *mut dyn FnOnce(u8) -> String;
    let callback = unsafe { Box::from_raw(pointer) };
    assert_eq!(callback(b'!'), "tag!");
}
