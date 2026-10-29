//@ edition: 2024
#![feature(never_type)]

fn keep<T, F: FnOnce() -> T>(callback: F) -> usize {
    drop(callback);
    std::mem::size_of::<T>()
}

fn require_never<F: FnOnce() -> !>(callback: F) -> u32 {
    drop(callback);
    17
}

fn unknown<T, F: FnOnce() -> T>() -> F {
    panic!("not called")
}

fn delayed<T>() -> u32 {
    if false {
        let mut callback = unknown::<T, _>();
        callback = || panic!("not called");
        drop(callback);
    }
    23
}

fn main() {
    assert_eq!(keep::<[u16; 3], _>(|| panic!("not called")), 6);
    assert_eq!(keep::<(), _>(|| panic!("not called")), 0);
    assert_eq!(require_never(|| panic!("not called")), 17);
    assert_eq!(delayed::<u8>(), 23);
}
