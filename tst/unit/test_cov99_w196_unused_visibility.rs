macro_rules! discard_visibility {
    ($unused:vis) => { 196 };
}

fn main() {
    assert_eq!(discard_visibility!(pub), 196);
}
