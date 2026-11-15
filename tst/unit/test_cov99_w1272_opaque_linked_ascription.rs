//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(type_alias_impl_trait, builtin_syntax, generic_arg_infer)]

pub type Hidden = impl Copy;

trait Family<const N: usize> {
    type Out;
}

impl Family<0> for () {
    type Out = Hidden;
}

impl Family<1> for () {
    type Out = u16;
}

type Linked<const N: usize>
where
    (): Family<N>,
= (<() as Family<N>>::Out, [(); N]);

fn tag<const N: usize>() -> [(); N] {
    [(); N]
}

#[define_opaque(Hidden)]
pub fn establish(mut value: Hidden) -> Hidden {
    let token = tag();
    let _ = builtin # type_ascribe((value, token), Linked<_>);
    let _: [(); 0] = token;
    value = 7u8;
    value
}
