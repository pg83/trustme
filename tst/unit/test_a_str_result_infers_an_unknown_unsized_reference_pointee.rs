fn unknown<T: ?Sized>() -> &'static T {
    loop {}
}

fn choose(known: bool) -> &'static str {
    if known {
        "inferred"
    } else {
        unknown()
    }
}

fn main() {
    assert_eq!(choose(true), "inferred");
}
