fn twice<F: Fn(u16) -> u16 + Copy + Clone>(callback: F, value: u16) -> u16 {
    let retained = callback;
    let cloned = callback.clone();
    retained(value) + cloned(value + 1)
}

fn main() {
    let step = |value: u16| value + 9;
    assert_eq!(twice(step, 7), 33);
    assert_eq!(step(11), 20);
    let independent = step.clone();
    assert_eq!(independent(13), 22);
}
