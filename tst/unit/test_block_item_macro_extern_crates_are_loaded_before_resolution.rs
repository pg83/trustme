fn main() {
    macro_rules! imports {
        () => {
            #[cfg(any())]
            extern crate unavailable_crate;
            extern crate core as block_core;
        };
    }
    imports!();
    assert_eq!(block_core::mem::size_of::<(u16, u32)>(), 8);
    {
        imports!();
        assert_eq!(block_core::mem::size_of::<[u16; 3]>(), 6);
    }
}
