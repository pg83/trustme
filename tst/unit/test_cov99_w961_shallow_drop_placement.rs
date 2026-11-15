//@ crate-type: lib
//@ aux-build: cov99_w961_drop_source.rs
//@ compile-flags: -Zshare-generics=yes

pub unsafe fn destroy(value: *mut cov99_w961_drop_source::Owned<u8>) {
    std::ptr::drop_in_place(value);
}
