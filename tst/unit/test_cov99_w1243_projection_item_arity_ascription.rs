//@ compile-fail: Family
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(type_ascription, builtin_syntax)]

trait Family {
    type Member;
}

impl Family for u8 {
    type Member = u8;
}

impl Family for u16 {
    type Member = u16;
}

fn member<T: Family>() -> T::Member {
    loop {}
}

fn main() {
    let _ = builtin # type_ascribe(member::<_>(), <_ as Family>::Member<()>);
}
