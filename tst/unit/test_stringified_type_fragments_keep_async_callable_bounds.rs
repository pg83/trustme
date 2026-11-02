#![feature(async_trait_bounds)]

macro_rules! spelling { ($ty:ty) => { stringify!($ty) }; }

fn compact(text: &str) -> String {
    text.chars().filter(|c| !c.is_whitespace()).collect()
}

fn main() {
    assert_eq!(compact(spelling!(impl async Fn(u16) -> u32 + Send)),
        "implasyncFn(u16)->u32+Send");
}
