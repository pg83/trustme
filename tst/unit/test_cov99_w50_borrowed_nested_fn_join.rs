fn first() -> u32 { 17 }
fn second() -> u32 { 29 }

fn choose(outer: bool, inner: bool) -> u32 {
    let selected = if outer {
        &(if inner { first } else { second })
    } else {
        &(first as fn() -> u32)
    };
    selected()
}

fn main() {
    assert_eq!(choose(true, true), 17);
    assert_eq!(choose(true, false), 29);
    assert_eq!(choose(false, true), 17);
    assert_eq!(choose(false, false), 17);
}
