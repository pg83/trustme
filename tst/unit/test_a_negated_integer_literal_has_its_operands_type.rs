fn main() {
    let data = [(1u8, -11)];
    let mut n = 0;
    for (a, b) in data.iter() {
        if *a > 0 && *b < 0 {
            n += 1;
        }
    }
    assert_eq!(n, 1);
    let flags = [(true, !0)];
    for (_, m) in flags.iter() {
        assert_eq!(*m, -1);
    }
}
