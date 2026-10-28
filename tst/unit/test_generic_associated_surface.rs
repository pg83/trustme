//@ crate-type: lib
#![allow(dead_code)]

trait Producer {
    type Item;
    fn produce(&self) -> Self::Item;
}

impl Producer for i32 {
    type Item = i32;
    fn produce(&self) -> i32 {
        *self
    }
}

trait Family {
    type Borrowed<'a>: Sized where Self: 'a;
    fn borrow<'a>(&'a self) -> Self::Borrowed<'a>;
}

impl Family for String {
    type Borrowed<'a> = &'a str;
    fn borrow<'a>(&'a self) -> &'a str {
        self.as_str()
    }
}

fn projection<T: Producer>(value: &T) -> T::Item {
    value.produce()
}

fn family(value: &String) -> usize {
    value.borrow().len()
}

fn opaque(value: i32) -> impl Producer {
    value
}
