#![feature(trait_alias)]

pub trait Source {
    type Item;
    fn next(&mut self) -> Self::Item;
}

pub trait WordSource = Source<Item = u16>;

pub struct Counter(pub u16);

impl Source for Counter {
    type Item = u16;

    fn next(&mut self) -> u16 {
        self.0 += 1;
        self.0
    }
}

pub fn adapt<S: WordSource, F: FnOnce(u16) -> u32>(source: &mut S, map: F) -> u32 {
    map(source.next())
}
