#![feature(generic_const_exprs, layout_for_ptr)]
#![allow(incomplete_features)]

const fn pointed_alignment<T>() -> usize {
    unsafe { core::mem::align_of_val_raw(0 as *const T) }
}

fn storage<T>(_: &T) -> [u8; pointed_alignment::<T>()] {
    [0; pointed_alignment::<T>()]
}

fn main() {
    let word = 41u64;
    let operation = move || word;
    let bytes = storage(&operation);
    assert_eq!(bytes.len(), core::mem::align_of::<u64>());
    assert_eq!(operation(), 41);
}
