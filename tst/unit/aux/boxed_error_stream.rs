use std::any::Any;

pub trait Stream {
    type Item;
    fn next(&mut self) -> Option<Self::Item>;
}

pub struct Caught<I>(pub I);

impl<I: Iterator> Stream for Caught<I> {
    type Item = Result<I::Item, Box<dyn Any + Send>>;
    fn next(&mut self) -> Option<Self::Item> {
        self.0.next().map(Ok)
    }
}
