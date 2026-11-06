const SEED: (u32, [u32; 2]) = (7, [11, 13]);

const fn alter() -> (u32, [u32; 2]) {
    let mut value = SEED;
    let head = &mut value.0;
    *head = 31;
    let tail = &mut value.1[1];
    *tail = 37;
    value
}

const ALTERED: (u32, [u32; 2]) = alter();

fn main() {
    assert_eq!(ALTERED, (31, [11, 37]));
    let mut value = SEED;
    let head = &mut value.0;
    *head = 41;
    let tail = &mut value.1[0];
    *tail = 43;
    assert_eq!(value, (41, [43, 13]));
}
