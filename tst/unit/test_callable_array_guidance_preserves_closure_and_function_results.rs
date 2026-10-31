fn increment(n: u16) -> u16 { n + 3 }
fn apply<F: Fn(u16) -> u16>(steps: [F; 3], mut n: u16) -> u16 {
    for step in steps { n = step(n); }
    n
}
fn main() {
    let steps = [increment as fn(u16) -> u16, |n| n * 5, increment];
    assert_eq!(apply(steps, 4), 38);
}
