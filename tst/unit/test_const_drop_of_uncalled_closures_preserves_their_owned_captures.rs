//@ compile-variant: -O
#![feature(const_closures, const_trait_impl, const_destruct)]

struct Plain { value: u16 }
union Resource { target: *mut u32 }

impl const Drop for Resource {
    fn drop(&mut self) {
        unsafe { *self.target += 19; }
    }
}

const fn evaluate() -> u32 {
    core::mem::drop(const || 113u16);
    let plain = Plain { value: 127 };
    core::mem::drop(const move || plain);
    let mut total = 23u32;
    let resource = Resource { target: &mut total };
    core::mem::drop(const move || resource);
    total
}

const TOTAL: u32 = evaluate();

fn main() {
    assert_eq!(TOTAL, 42);
    assert_eq!(evaluate(), 42);
}
