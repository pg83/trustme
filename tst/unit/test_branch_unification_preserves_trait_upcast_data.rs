//@ compile-variant: -O

trait Base { fn value(&self) -> u32; }
trait Derived: Base { fn extra(&self) -> u32; }
struct First(u32);
struct Second(u32);
impl Base for First { fn value(&self) -> u32 { self.0 } }
impl Derived for First { fn extra(&self) -> u32 { self.0 + 1 } }
impl Base for Second { fn value(&self) -> u32 { self.0 * 2 } }
impl Derived for Second { fn extra(&self) -> u32 { self.0 + 3 } }

#[inline(never)]
fn same(flag: bool, a: &dyn Derived) -> &dyn Base {
    if flag { a } else { a }
}

#[inline(never)]
fn different<'a>(flag: bool, a: &'a dyn Derived, b: &'a dyn Derived) -> &'a dyn Base {
    if flag { a } else { b }
}

fn main() {
    let first = First(19);
    let second = Second(37);
    let a: &dyn Derived = &first;
    let b: &dyn Derived = &second;
    assert_eq!(a.extra(), 20);
    assert_eq!(b.extra(), 40);
    for flag in [false, true] {
        assert_eq!(same(flag, a).value(), 19);
        assert_eq!(different(flag, a, b).value(), if flag { 19 } else { 74 });
    }
}
