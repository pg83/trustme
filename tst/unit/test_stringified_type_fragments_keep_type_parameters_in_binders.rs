#![feature(non_lifetime_binders)]
#![allow(incomplete_features)]

macro_rules! spelling { ($ty:ty) => { stringify!($ty) }; }

fn compact(text: &str) -> String {
    text.chars().filter(|c| !c.is_whitespace()).collect()
}

fn main() {
    assert_eq!(compact(spelling!(for<T, U> fn(T, U) -> (U, T))),
        "for<T,U>fn(T,U)->(U,T)");
    assert_eq!(compact(spelling!(for<'a, T, U> fn(&'a T, U) -> U)),
        "for<'a,T,U>fn(&'aT,U)->U");
}
