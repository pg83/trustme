//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

pub trait Optional {
    type Hidden where Self: Sized;
}

pub fn forget_binding(pointer: *const *const dyn Optional<Hidden = u8>) -> *const *const dyn Optional {
    pointer as *const *const dyn Optional
}
