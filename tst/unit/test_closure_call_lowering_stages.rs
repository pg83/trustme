const NESTED: fn(u32) -> u32 = |start| {
    let add = |value| start + value;
    add(4)
};

fn main() {
    assert_eq!(NESTED(3), 7);

    let base = 8;
    let shared = |value| base + value;
    assert_eq!((&shared)(3), 11);
    assert_eq!(shared(5), 13);

    let mut value = 5;
    {
        let mut update = |increment| {
            value += increment;
            value
        };
        assert_eq!((&mut update)(2), 7);
        assert_eq!(update(3), 10);
    }
    assert_eq!(value, 10);

    let owned = String::from("lowered");
    let once = move || {
        let inner = move || owned;
        inner()
    };
    assert_eq!(once(), "lowered");

    let no_capture = (|| |value: i32| value * 2)();
    assert_eq!(no_capture(9), 18);
}
