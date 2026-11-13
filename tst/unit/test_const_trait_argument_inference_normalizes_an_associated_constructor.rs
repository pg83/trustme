#![feature(generic_arg_infer)]

struct Buffer<const N: usize>([u16; N]);

impl<const N: usize> Buffer<N> {
    fn filled(value: u16) -> Self {
        Self([value; N])
    }
}

trait Build<const N: usize> {
    type Output;
}

struct Select;
impl Build<3> for Select {
    type Output = Buffer<3>;
}

fn main() {
    let value = <Select as Build<_>>::Output::filled(479);
    assert_eq!(value.0.len(), 3);
    assert_eq!(value.0[2], 479);
}
