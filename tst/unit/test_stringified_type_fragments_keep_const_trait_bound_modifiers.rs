#![feature(const_trait_impl)]

macro_rules! spelling { ($ty:ty) => { stringify!($ty) }; }

fn compact(text: &str) -> String {
    text.chars().filter(|c| !c.is_whitespace()).collect::<String>()
        .replace("~const", "[const]")
}

fn main() {
    assert_eq!(compact(spelling!(impl const Stamp + Send)), "implconstStamp+Send");
    assert_eq!(compact(spelling!(impl ~const Stamp + Send)), "impl[const]Stamp+Send");
}
