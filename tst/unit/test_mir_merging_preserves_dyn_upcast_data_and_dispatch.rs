//@ compile-variant: -O
trait Base { fn read(&self) -> u16; }
trait Derived: Base { fn offset(&self) -> u16; }
struct Entry(u16);
impl Base for Entry { fn read(&self) -> u16 { self.0 } }
impl Derived for Entry { fn offset(&self) -> u16 { self.0 + 7 } }

#[inline(never)]
fn select<'a>(first: bool, left: &'a dyn Derived, right: &'a dyn Derived) -> &'a dyn Base {
    if first { left as &dyn Base } else { right as &dyn Base }
}

#[inline(never)]
fn repeat(first: bool, value: &dyn Derived) -> &dyn Base {
    if first { value as &dyn Base } else { value as &dyn Base }
}

fn main() {
    let left = Entry(41);
    let right = Entry(73);
    for first in [false, true] {
        let selected = select(first, &left, &right);
        assert_eq!(selected.read(), if first { 41 } else { 73 });
        let actual = selected as *const dyn Base as *const ();
        let expected = if first { &left as *const Entry } else { &right as *const Entry } as *const ();
        assert_eq!(actual, expected);
        assert_eq!(repeat(first, &left).read(), 41);
    }
    assert_eq!((&left as &dyn Derived).offset(), 48);
}
