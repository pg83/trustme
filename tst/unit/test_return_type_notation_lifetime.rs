#![feature(return_type_notation)]

trait Factory {
    fn borrow(&mut self) -> impl Sized + '_;
}

fn require_static<T: Factory<borrow(..): 'static>>() {}

fn main() {}
