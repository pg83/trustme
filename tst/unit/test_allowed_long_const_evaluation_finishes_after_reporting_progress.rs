//@ compile-flags: -Along_running_const_eval

const fn count() -> u32 {
    let mut value = 0;
    while value < 4_194_320 {
        value += 1;
    }
    value
}

const COUNT: u32 = count();

fn main() {
    assert_eq!(COUNT, 4_194_320);
}
