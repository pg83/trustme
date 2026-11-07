const VALUES: (&[u16], &(u16, u16, u16)) = (
    &[11u16, 13, 17] as &[u16],
    &match *&[19u16, 23, 29] {
        [first, middle, last] => (last, first, middle),
    },
);

fn promoted() -> &'static (&'static [u16], &'static [u16; 3]) {
    &(&[31u16, 37, 41] as &[u16], &[43u16, 47, 53])
}

fn main() {
    assert_eq!(VALUES.0, &[11, 13, 17]);
    assert_eq!(*VALUES.1, (29, 19, 23));
    let nested = promoted();
    assert_eq!(nested.0, &[31, 37, 41]);
    assert_eq!(*nested.1, [43, 47, 53]);
    assert_eq!(nested.0.len(), 3);
    assert_eq!(nested.1[1], 47);
}
