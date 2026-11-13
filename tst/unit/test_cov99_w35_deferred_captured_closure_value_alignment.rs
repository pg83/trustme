#![feature(type_alias_impl_trait, layout_for_ptr)]

type Callback = impl Fn() -> u64 + Copy;

#[define_opaque(Callback)]
fn callback(value: u64) -> Callback {
    move || value
}

type Storage = [u8; unsafe {
    core::mem::align_of_val_raw(0 as *const Callback)
}];

fn main() {
    let operation = callback(37);
    let bytes: Storage = [0; core::mem::align_of::<u64>()];
    assert_eq!(bytes.len(), core::mem::align_of_val(&operation));
    assert_eq!(operation(), 37);
}
