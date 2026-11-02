//@ proc-macro-aux-build: type_phase_identity.rs
extern crate type_phase_identity as proxy;

macro_rules! generic {
    ($name:ident, $parameter:ident, $ty:ty) => {
        proxy::emit! { fn $name<$parameter>(value: $ty) -> $ty { value } }
    };
}

macro_rules! alias {
    ($name:ident, $ty:ty) => { proxy::emit! { type $name = $ty; } };
}

generic!(identity, T, T);
alias!(Word, u16);
alias!(Callback, fn(Word) -> Word);

fn increment(value: Word) -> Word { value + 7 }

fn main() {
    let callback: Callback = identity(increment as Callback);
    assert_eq!(callback(11), 18);
    assert_eq!(identity(String::from("type")), "type");
}
