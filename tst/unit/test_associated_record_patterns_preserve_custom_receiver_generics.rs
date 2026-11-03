//@ compile-variant: -O
#![feature(more_qualified_paths)]

use std::rc::Rc;

struct Record<T> { value: T }
struct Wrapper<T>(T);

trait Project { type Output; }
impl<T> Project for Wrapper<T> { type Output = Record<T>; }

impl<T: Copy> Wrapper<T> {
    fn extract(self: Rc<Self>) -> T {
        let <Wrapper<T> as Project>::Output { value } = Record { value: self.0 };
        value
    }
}

fn main() {
    assert_eq!(Rc::new(Wrapper(941u16)).extract(), 941u16);
    assert_eq!(Rc::new(Wrapper('Щ')).extract(), 'Щ');
}
