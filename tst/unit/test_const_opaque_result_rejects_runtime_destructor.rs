//@ compile-fail: cannot be evaluated at compile-time

struct Resource(u8);
impl Drop for Resource { fn drop(&mut self) {} }
const fn selected() -> impl Sized { Resource(83) }

const CHECK: () = { let _value = selected(); };

fn main() { let _ = CHECK; }
