trait Family {
    type Input;
    type Output;
}

impl Family for u8 {
    type Input = u8;
    type Output = u8;
}

struct Runner;

impl Runner {
    const fn convert<T: Family<Input = u8, Output = u8>>(value: T::Input) -> T::Output {
        value
    }
}

trait Length {
    const TAG: usize;
}

impl Length for [u8; 3] {
    const TAG: usize = 17;
}

type Selected = [u8; <[u8; Runner::convert::<u8>(3) as usize]>::TAG];

fn main() {
    let value: Selected = [19; 17];
    assert_eq!(value, [19; 17]);
}
