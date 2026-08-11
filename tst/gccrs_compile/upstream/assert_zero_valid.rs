pub fn zeroed_integer() -> i32 {
    unsafe { std::mem::MaybeUninit::<i32>::zeroed().assume_init() }
}

pub fn zeroed_reference_storage() -> std::mem::MaybeUninit<&'static i32> {
    std::mem::MaybeUninit::zeroed()
}
