//@ compile-fail: cannot be evaluated at compile-time
#![feature(pattern_types, pattern_type_macro)]

struct Resource(u8);
impl Drop for Resource { fn drop(&mut self) {} }
type Restricted = std::pat::pattern_type!(Resource is !null);

const CHECK: () = {
    let _value: Restricted = unsafe { std::mem::transmute(Resource(73)) };
};

fn main() { let _ = CHECK; }
