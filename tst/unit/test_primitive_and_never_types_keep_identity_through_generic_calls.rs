fn identity<T>(value: T) -> T {
    value
}

fn diverges() -> ! {
    loop {}
}

fn main() {
    assert_eq!(identity(-7i8), -7i8);
    assert_eq!(identity(-257i16), -257i16);
    assert_eq!(identity(-65537i32), -65537i32);
    assert_eq!(identity(-4294967297i64), -4294967297i64);
    assert_eq!(identity(-17i128), -17i128);
    assert_eq!(identity(-23isize), -23isize);
    assert_eq!(identity(7u8), 7u8);
    assert_eq!(identity(257u16), 257u16);
    assert_eq!(identity(65537u32), 65537u32);
    assert_eq!(identity(4294967297u64), 4294967297u64);
    assert_eq!(identity(17u128), 17u128);
    assert_eq!(identity(23usize), 23usize);
    assert_eq!(identity(1.25f32) + identity(0.5f32), 1.75f32);
    assert_eq!(identity(2.5f64), 2.5f64);
    assert!(identity(true));
    assert_eq!(identity('λ'), 'λ');
    assert_eq!(identity("cached"), "cached");
    let pointer: fn() -> ! = diverges;
    let same = identity(pointer);
    assert_eq!(same as usize, pointer as usize);
    assert_eq!(
        std::any::type_name_of_val(&same),
        std::any::type_name::<fn() -> !>()
    );
}
