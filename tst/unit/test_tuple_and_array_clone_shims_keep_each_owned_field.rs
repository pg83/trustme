//@ compile-variant: -O
use std::cell::Cell;

struct Token<'a> { value: u16, clones: &'a Cell<u16>, drops: &'a Cell<u16> }
impl Clone for Token<'_> {
    fn clone(&self) -> Self {
        self.clones.set(self.clones.get() + 1);
        Self { value: self.value, clones: self.clones, drops: self.drops }
    }
}
impl Drop for Token<'_> {
    fn drop(&mut self) { self.drops.set(self.drops.get() + self.value); }
}
fn main() {
    let clones = Cell::new(0);
    let drops = Cell::new(0);
    let pair = (Token { value: 3, clones: &clones, drops: &drops }, Token { value: 5, clones: &clones, drops: &drops });
    let copied = pair.clone();
    assert_eq!(clones.get(), 2);
    drop(pair);
    assert_eq!(drops.get(), 8);
    drop(copied);
    assert_eq!(drops.get(), 16);
    let values = [Token { value: 7, clones: &clones, drops: &drops }, Token { value: 11, clones: &clones, drops: &drops }];
    let copied = values.clone();
    assert_eq!(clones.get(), 4);
    drop(values);
    drop(copied);
    assert_eq!(drops.get(), 52);
}
