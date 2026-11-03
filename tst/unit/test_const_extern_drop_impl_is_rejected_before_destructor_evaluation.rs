//@ compile-fail: Drop can only be implemented
#![feature(extern_types, const_trait_impl, const_destruct)]

unsafe extern "C" { type Foreign; }

impl const Drop for Foreign {
    fn drop(&mut self) {}
}

fn main() {}
