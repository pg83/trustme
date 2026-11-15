//@ crate-type: lib
//@ compile-flags: --emit=dep-info
//@ proc-macro-aux-build: cov99_w958_empty_vtable_slot_macro.rs

cov99_w958_empty_vtable_slot_macro::empty_slot!();

pub trait Slot {
    fn get(&self);
}

pub fn method_pointer() -> fn(&'static dyn Slot) {
    <dyn Slot as Slot>::get
}
