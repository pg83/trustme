//@ reject-flags: --edition=2021 --cfg missing => error || error
//@ reject-flags: --edition=2021 --cfg function => error || error
//@ reject-flags: --edition=2021 --cfg trait_middle => error || error
//@ reject-flags: --edition=2021 --cfg trait_end => error || error
//@ reject-flags: --edition=2021 --cfg variant => error || error
mod seed {
    pub mod leaf { pub const VALUE: u8 = 31; }
    pub fn worker() {}
    pub trait Shape { type Item; }
    pub enum Choice { First }
}
#[cfg(missing)] use seed::leaf::absent::Item;
#[cfg(function)] use seed::worker::Item;
#[cfg(trait_middle)] use seed::Shape::Item::Leaf;
#[cfg(trait_end)] use seed::Shape::Item;
#[cfg(variant)] use seed::Choice::First::Item;
fn main() {
    use seed::leaf::VALUE;
    assert_eq!(VALUE, 31);
}
