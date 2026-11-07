struct Stamp<const N: usize = {
    let make = &|| {
        let word = 1201usize;
        word + 8
    };
    std::mem::size_of_val(make)
}>;

fn main() {
    let _stamp = Stamp::<7>;
    assert_eq!(std::mem::size_of::<Stamp<7>>(), 0);
}
