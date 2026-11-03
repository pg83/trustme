#![feature(inherent_associated_types, never_type)]

struct Pocket<T>(T);
impl<T> Pocket<T> {
    type Read = *const T;
    type Write = *mut T;
    type End = !;
}

fn forever() -> Pocket<u32>::End { loop {} }

fn main() {
    let mut value = 31u32;
    let read: Pocket<u32>::Read = &value;
    assert_eq!(unsafe { *read }, 31);
    let write: Pocket<u32>::Write = &mut value;
    unsafe { *write = 43; }
    assert_eq!(value, 43);
    let callback: fn() -> Pocket<u32>::End = forever;
    assert_eq!(std::mem::size_of_val(&callback), std::mem::size_of::<usize>());
}
