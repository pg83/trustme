fn make(value: u16) -> impl Fn() -> u16 + Clone + Copy {
    move || value
}

fn main() {
    let mut target = make(61);
    let source = make(67);
    let saved = target;
    target.clone_from(&source);
    assert_eq!(target(), 67);
    assert_eq!(source(), 67);
    assert_eq!(saved(), 61);

    let mut pair = (71u16, true);
    let replacement = (73u16, false);
    let saved_pair = pair;
    pair.clone_from(&replacement);
    assert_eq!(pair, (73, false));
    assert_eq!(replacement, (73, false));
    assert_eq!(saved_pair, (71, true));
}
