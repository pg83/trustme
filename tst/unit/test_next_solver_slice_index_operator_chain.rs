//@ check-pass
//@ compile-flags: -Znext-solver

fn fill<const OFFSET: usize>(buffer: &mut [u8]) {
    for index in (0..4).rev() {
        buffer[index * 4 + OFFSET + 3] = 1;
    }
}

fn main() {
    let mut buffer = [0; 8];
    fill::<0>(&mut buffer);
    assert_eq!(buffer[3], 1);
}
