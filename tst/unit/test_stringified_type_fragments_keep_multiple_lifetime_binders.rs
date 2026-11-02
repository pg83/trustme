macro_rules! spelling { ($ty:ty) => { stringify!($ty) }; }

fn compact(text: &str) -> String {
    text.chars().filter(|c| !c.is_whitespace()).collect()
}

fn main() {
    assert_eq!(compact(spelling!(for<'a, 'b> fn(&'a u8, &'b u16) -> (&'b u16, &'a u8))),
        "for<'a,'b>fn(&'au8,&'bu16)->(&'bu16,&'au8)");
    assert_eq!(compact(spelling!(dyn for<'a, 'b> Fn(&'a str, &'b [u8]) -> usize + Send)),
        "dynfor<'a,'b>Fn(&'astr,&'b[u8])->usize+Send");
}
