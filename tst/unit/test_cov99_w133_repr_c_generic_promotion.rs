#[repr(C)]
enum CEnum<T> {
    Data(T),
    Empty,
}

fn empty<T: Copy + 'static>() -> &'static CEnum<T> {
    &CEnum::Empty
}

fn main() {
    assert!(matches!(empty::<u64>(), CEnum::Empty));
}
