fn main() {
    let pair = (String::from("left"), String::from("right"));
    let consume = || {
        let total = pair.0.len() + pair.1.len();
        drop(pair);
        total
    };
    assert_eq!(consume(), 9);
    let mut nested = ((String::from("a"), String::from("bc")), 7);
    let mut replace = || {
        nested.0.0.push('d');
        nested.0.1.push('e');
        let old = std::mem::replace(&mut nested.0, (String::from("f"), String::from("g")));
        old.0.len() + old.1.len()
    };
    assert_eq!(replace(), 5);
    assert_eq!(nested.0.0, "f");
    assert_eq!(nested.0.1, "g");
    assert_eq!(nested.1, 7);
}
