#![feature(const_drop_in_place, const_trait_impl, const_destruct)]

enum Never {}
struct Mark(*mut u32, u32);

impl const Drop for Mark {
    fn drop(&mut self) {
        unsafe { *self.0 = *self.0 * 100 + self.1; }
    }
}

enum Packet { Before(Never), Present(Mark, Mark), After(Never) }

const fn evaluate() -> u32 {
    let mut order = 3;
    let pointer = &raw mut order;
    let mut packet = std::mem::ManuallyDrop::new(Packet::Present(Mark(pointer, 7), Mark(pointer, 11)));
    unsafe { std::ptr::drop_in_place(&raw mut packet as *mut Packet); }
    order
}

const ORDER: u32 = evaluate();

fn main() {
    assert_eq!(ORDER, 30711);
    assert_eq!(evaluate(), ORDER);
}
