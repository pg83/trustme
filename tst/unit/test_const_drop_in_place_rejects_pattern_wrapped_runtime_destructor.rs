//@ compile-fail: cannot be evaluated at compile-time
#![feature(pattern_types, pattern_type_macro, const_drop_in_place, const_trait_impl, const_destruct)]

struct Resource(u8);
impl Drop for Resource { fn drop(&mut self) {} }
type Restricted = std::pat::pattern_type!(Resource is !null);

const CHECK: () = {
    let mut value = std::mem::ManuallyDrop::new(Resource(79));
    unsafe {
        std::ptr::drop_in_place((&raw mut value as *mut Resource).cast::<Restricted>());
    }
};

fn main() { let _ = CHECK; }
