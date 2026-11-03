fn step(value: u32) -> u32 { value + 2 }
fn main() {
    let mut callbacks: [Option<fn(_) -> _>; 2] = [Some(step), None];
    callbacks[1] = Some(|value| value * 3);
    let results = callbacks.into_iter().flatten().map(|callback| callback(11)).collect::<Vec<_>>();
    assert_eq!(results, [13, 33]);
    let pair: (_, fn(_) -> _) = ([17u16, 19], |value: i32| value - 1);
    assert_eq!(pair.0, [17, 19]);
    assert_eq!(pair.1(23), 22);
}
