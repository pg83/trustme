#![feature(const_trait_impl)]

macro_rules! spelling { ($ty:ty) => { stringify!($ty) }; }

fn main() {
    let text = spelling!(impl [const] Stamp + Send);
    let compact: String = text.chars().filter(|c| !c.is_whitespace()).collect();
    assert_eq!(compact.replace("~const", "[const]"), "impl[const]Stamp+Send");
}
