//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(ptr_metadata)]

struct Check;

impl Check {
    fn require<T>(&self, _: T)
    where
        (u8, T): std::ptr::Pointee<Metadata = ()>,
    {
    }
}

fn main() {
    Check.require(7u32);
}
