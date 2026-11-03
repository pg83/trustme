fn stage<T: Copy, F: FnMut(T) -> T>(mut values: Vec<T>, mut callback: F, seed: T) -> (Vec<T>, [T; 2], Option<T>) {
    let selected = callback(seed);
    values.push(selected);
    (values, [selected; 2], Some(selected))
}
fn main() {
    let values = Vec::new();
    let result = stage(values, |value| value + 3, 233u32);
    assert_eq!(result.0, [236]);
    assert_eq!(result.1, [236, 236]);
    assert_eq!(result.2, Some(236));
}
