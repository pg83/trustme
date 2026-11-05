//@ compile-variant: -O

trait Base { fn base(&self) -> u32; }
trait Child: Base { fn child(&self) -> u32; }
struct Number(u32);
impl Base for Number { fn base(&self) -> u32 { self.0 + 13 } }
impl Child for Number { fn child(&self) -> u32 { self.0 + 17 } }

#[inline(never)]
fn same(first: bool, value: &dyn Child) -> &dyn Base {
    if first { value as &dyn Base } else { value as &dyn Base }
}

#[inline(never)]
fn different<'a>(first: bool, left: &'a dyn Child, right: &'a dyn Child) -> &'a dyn Base {
    if first { left as &dyn Base } else { right as &dyn Base }
}

fn main() {
    let left = Number(29);
    let right = Number(31);
    let left_child: &dyn Child = &left;
    let right_child: &dyn Child = &right;
    for first in [true, false] {
        assert_eq!(same(first, left_child).base(), 42);
        let selected = different(first, left_child, right_child);
        assert_eq!(selected.base(), if first { 42 } else { 44 });
        assert_eq!(std::ptr::addr_eq(selected, if first { left_child } else { right_child }), true);
    }
    assert_eq!(left_child.child(), 46);
    assert_eq!(right_child.child(), 48);
}
