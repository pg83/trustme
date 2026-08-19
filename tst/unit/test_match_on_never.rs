// Nothing inhabits `!`, so matching a value of that type is dead code -- it
// still has to compile, whatever the arms hold.
const FOO: &&&u32 = &&&42;

fn unreachable_match() -> u32 {
    match panic!("gone") {
        &&&42 => 1,
        FOO => 2,
        _ => 3,
    }
}

fn main() {
    let live = 5u32;
    let n = match live {
        5 => 10,
        _ => 0,
    };
    assert_eq!(n, 10);
    let _ = unreachable_match;
}
