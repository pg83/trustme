fn main() {
    let offset = 7u16;
    let add = |value| value + offset;
    let shared = &add as *const dyn Fn(u16) -> u16;
    assert_eq!(unsafe { (&*shared)(5) }, 12);

    let mut total = 10u32;
    let mut accumulate = |value| { total += value; total };
    let unique = &mut accumulate as *mut dyn FnMut(u32) -> u32;
    assert_eq!(unsafe { (&mut *unique)(3) }, 13);
    assert_eq!(unsafe { (&mut *unique)(4) }, 17);
    assert_eq!(total, 17);
}
