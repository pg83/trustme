//@ crate-type: lib
//@ env: RUSTC_OVERRIDE_VERSION_STRING=1.93.0
//@ compile-flags: -Zstop-after=expand

#[derive(Clone, Copy)]
pub union Storage<'a, T: Copy + 'a, const N: usize> {
    pub reference: &'a T,
    pub bytes: [u8; N],
}

pub fn clone_entry<'a>(value: Storage<'a, u16, 7>) -> Storage<'a, u16, 7> {
    value.clone()
}
