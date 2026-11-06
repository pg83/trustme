fn choose(left: bool) -> u16 {
    let mut mutable = [17u16; 3];
    let shared = [29u16; 3];
    let selected = if left { &mut mutable } else { &shared };
    selected[1]
}

fn main() {
    assert_eq!(choose(true), 17);
    assert_eq!(choose(false), 29);
}
