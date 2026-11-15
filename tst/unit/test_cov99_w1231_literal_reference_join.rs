fn main() {
    for flag in [true, false] {
        let selected: &_ = match flag {
            true => &11,
            false => &29,
        };
        if flag {
            assert_eq!(*selected, 11);
        } else {
            assert_eq!(*selected, 29);
        }
    }
}
